#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>

#include "secrets.h"

// Phase 1 — brake light on PWM, status LED and mode button.
// Phase 2 — two line sensors on GPIO 34 and 35, read and reported together with
// the difference between them. Their reference values have been measured on the
// bench; see the comment beside the pin definitions. Phase 2 also brings up the
// HC-SR04 on GPIO 5 and 18, which reports the echo pulse next to the distance
// it converts to, median-filtered over three samples. Still data collection
// only: there are no thresholds and no driving decisions in this file yet. Pins
// follow the wiring contract in README.md. GPIO 16/17 from the original plan
// are not broken out on this 30-pin board, so the brake light is on 19 and the
// status LED on 13.
//
// Phase 4 — the rover brings up its own Wi-Fi access point. Nothing serves or
// listens on it yet: no dashboard, no web server, no driving commands. Those
// come as separate steps, deliberately, because a network and a server brought
// up together and failing together say nothing about which of the two broke.
//
// The PWM here runs on the brake light only because it is already wired and the
// result is visible. In the finished rover the brake light is on/off, and this
// same mechanism drives the L298N enable pins for motor speed.

// --- Pin assignments ---
constexpr uint8_t PIN_BRAKE_LIGHT = 19;
constexpr uint8_t PIN_STATUS_LED = 13;
constexpr uint8_t PIN_MODE_BUTTON = 23;

// The AEB warning output. This is a three-pin module — GND, I/O, VCC — with a
// transistor on the board, so the I/O line is a control input and draws very
// little current from the pin. The buzzer is the active kind, with its own
// oscillator inside, so holding the pin HIGH is enough to make a sound. It
// needs neither tone() nor PWM, and driving it with either would only fight the
// oscillator it already has.
constexpr uint8_t PIN_BUZZER = 4;

// These modules are sold in both polarities and this one was settled by test,
// not by assumption: with the pin driven LOW it sounds, so LOW is the active
// level and it needs a HIGH to stay quiet.
//
// The consequence worth knowing is that an undriven pin is not a quiet one. The
// module sounded continuously from the moment it was wired, before any firmware
// touched GPIO 4, and it will do the same in the window between a reset and the
// line below that silences it. For the AEB stage that cuts both ways: a crashed
// or resetting board announces itself, which is not the worst behaviour for a
// safety warning, but it also means silence depends on the firmware being
// alive.
constexpr uint8_t BUZZER_SOUND = LOW;
constexpr uint8_t BUZZER_SILENT = HIGH;

// --- L298N motor driver ---
// Four motors on two channels: the two on each side are wired in parallel into
// one output pair, so the driver — and the code — only ever sees two sides.
// ENA and ENB carry the PWM that sets speed. IN1/IN2 and IN3/IN4 are plain
// digital pins whose pattern sets the direction of their side.
constexpr uint8_t PIN_MOTOR_ENA = 32; // left side speed
constexpr uint8_t PIN_MOTOR_IN1 = 33; // left side direction
constexpr uint8_t PIN_MOTOR_IN2 = 25;
constexpr uint8_t PIN_MOTOR_IN3 = 26; // right side direction
constexpr uint8_t PIN_MOTOR_IN4 = 27;
constexpr uint8_t PIN_MOTOR_ENB = 14; // right side speed

// Observed on the bench: given the same duty on both sides, the right side runs
// slightly weaker than the left. Two nominally identical motors never are —
// gearbox friction, brush wear and winding tolerance all differ — so a trim
// belongs here eventually, applied inside drive() so that no caller can forget
// it.
//
// Nothing is applied yet, on purpose. A 15% boost on the right was tried and
// overshot visibly, which puts the real figure somewhere below that but does
// not say where. Guessing again would just trade one wrong number for another.
// The value gets measured on a straight run over a marked distance, once the
// rover drives on the floor rather than on a stand.

// Both line sensors sit on ADC1. ADC2 cannot be used while Wi-Fi is running
// because it shares hardware with the radio, so every analog sensor in this
// project stays on GPIO 32-39. Only the modules' analog outputs are wired: the
// LKA has to know how far a sensor has gone into a stripe, not merely that it
// has, and the digital output cannot say that.
//
// These are replacement modules. The pair calibrated in phase 2 was swapped for
// different units of the same model, so none of those figures carry over. What
// follows was measured on the assembled vehicle, at the mounted height of
// 3.75 cm, over the materials the track is made of: white bristol board and
// black tape. Optical return depends on the material and on the geometry, so a
// bench measurement over generic paper does not describe this setup.
//
// The direction is the opposite of the intuitive one — a HIGH value means a
// DARK surface. The rover runs inside a white lane marked by two black stripes,
// one sensor to a side, so while it stays in the lane both sensors read low. A
// high reading means that sensor has reached a stripe, which is what a
// deviation looks like. The last two rows below are those deviation cases;
// neither of them is a centred reference.
//
//   both over white    L ~1417   R ~1691
//   left over black    L ~3250   R ~1677
//   right over black   L ~1354   R ~3436
//
//   span white->black  L 1833    R 1745
//
// Two separate uncertainties, and the larger is the one to design against. Each
// printed reading is already an average of 16 samples; successive readings, 200
// ms apart with nothing touched, repeat to about +/-5 counts. Between separate
// measurements the value drifts by up to 65.
//
// White and black stay well apart under that drift. A threshold shared by both
// sensors works — anything from 1691 to 3250 classifies every reading
// correctly, with about 780 counts of margin in the worst case. What survives
// the drift is that separation, not the absolute values.
//
// What the code must not assume is that the two are interchangeable. They read
// 274 counts apart over the same white, so the raw difference L - R rests at
// -274 rather than at zero, and the offset moves between builds — it was 330 on
// the previous pair. Nothing corrects for it yet and its cause is not isolated.
//
// The measurements and the reasoning behind them are in docs/journal.md,
// session 12.
constexpr uint8_t PIN_LINE_SENSOR_LEFT = 34;
constexpr uint8_t PIN_LINE_SENSOR_RIGHT = 35;

// The HC-SR04 is a 5 V part, fed here from VIN at 5.1 V, so its ECHO line
// swings to that level. GPIO 18 is not 5 V tolerant, so ECHO arrives through a
// 1k/2k divider, measured at 3.42 V for a 5.1 V pulse. TRIG needs no divider in
// the other direction: the module reads a 3.3 V output as HIGH.
constexpr uint8_t PIN_ULTRASONIC_TRIG = 5;
constexpr uint8_t PIN_ULTRASONIC_ECHO = 18;

// --- Ultrasonic timing ---
// A measurement starts with a 10 us HIGH pulse on TRIG, which is what the
// module listens for. The short LOW in front of it puts the line at a known
// level first, so the module sees one clean rising edge rather than one riding
// on whatever the pin was already doing.
constexpr unsigned long TRIG_SETTLE_US = 2;
constexpr unsigned long TRIG_PULSE_US = 10;

// pulseIn defaults to a one-second timeout, which would freeze the loop for a
// full second every time an echo goes missing. The module is rated to 400 cm,
// and sound covers that out and back in about 23.3 ms, so 25 ms spans the whole
// usable range while still giving up quickly when nothing comes back.
constexpr unsigned long ECHO_TIMEOUT_US = 25000;

// Speed of sound at room temperature — 343 m/s, written in the units the echo
// is actually measured in.
constexpr float SOUND_SPEED_CM_PER_US = 0.0343f;

// --- OLED (I2C) ---
// The ESP32 can put I2C on almost any pin, so the pair is named rather than
// left to the core's default.
constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// Confirmed by the bus scanner in tools/i2c_scanner rather than assumed: it
// reported exactly one device, at this address.
constexpr uint8_t OLED_ADDRESS = 0x3C;

// A 0.91" panel is 128x32. Getting this wrong does not fail loudly — the
// library still draws, into a buffer the wrong shape — so the test screen below
// puts a border on the outer pixels to make a mismatch visible at a glance.
constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 32;

// The library raises the bus to 400 kHz while it transfers and drops it back
// afterwards. The scanner in tools/ talks to this same panel at the core's
// default 100 kHz and finds it every single time, so 400 kHz is the one thing
// that differs between the case that works and the case that does not. Long
// breadboard jumpers with no external pull-ups add stray capacitance, which
// rounds off the signal edges; at 100 kHz there is time for them to settle, at
// 400 kHz there is not, and the controller receives corrupted commands.
constexpr uint32_t I2C_CLOCK_HZ = 100000UL;

// Text layout for the test screen, inset far enough to clear the border.
constexpr int16_t OLED_TEXT_LEFT = 4;
constexpr int16_t OLED_TEXT_TOP = 4;
constexpr int16_t OLED_LINE_SPACING = 9;

// -1 says there is no reset pin: this module has four pins and exposes none.
constexpr int8_t OLED_RESET_PIN = -1;

// The last two arguments are the bus speed during a transfer and after it. Both
// are pinned to the same value so nothing switches the bus behind our back.
Adafruit_SSD1306 display(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RESET_PIN,
                         I2C_CLOCK_HZ, I2C_CLOCK_HZ);

// --- Ultrasonic filtering ---
// Each measurement takes three samples and keeps the middle one. A median
// rather than an average, because the two fail differently: an average is
// dragged toward a bad sample in proportion to how bad it is, while a median
// steps over it — one wild value out of three cannot end up in the middle. That
// distinction matters because ultrasonic errors have exactly that shape. A
// missed echo, or one picked up off a wall at an angle, lands far from the
// truth rather than slightly off it, so a single one visibly moves an average
// and does not move a median at all.
constexpr uint8_t DISTANCE_SAMPLES = 3;

// How many of those samples have to come back with an echo before the result
// counts as a measurement at all.
constexpr uint8_t DISTANCE_MIN_VALID_SAMPLES = 2;

// The bursts need spacing, or the next measurement hears the tail of the
// previous one still bouncing around the room and reads it as a much closer
// object.
constexpr unsigned long SAMPLE_SPACING_MS = 5;

// --- ADC ---
// 12 bits gives a 0-4095 range, and ADC_11db widens the input range so the
// whole 0-3.3 V the sensor can put out is measurable.
constexpr uint8_t ADC_RESOLUTION_BITS = 12;

// The ADC is noisy: repeated reads of a steady voltage still differ by tens of
// counts. Averaging a burst of samples narrows that spread.
constexpr uint8_t LINE_SENSOR_SAMPLES = 16;

// --- PWM (LEDC) ---
// LEDC is a peripheral inside the ESP32, not a software loop. Once a channel is
// configured it keeps generating the waveform on its own, with no work from the
// CPU, which leaves the main loop free to read sensors.
//
// The eight channels share four timers in pairs — 0-1, 2-3, 4-5, 6-7 — and the
// frequency belongs to the timer, not to the channel. Two channels on the same
// timer therefore cannot run at different frequencies: setting one silently
// changes the other, with no error raised. The motors will want a frequency of
// their own, so they are reserved here on channels that sit on separate timers.
// Declared before the L298N is wired so the allocation cannot be taken by
// accident later.
constexpr uint8_t PWM_CHANNEL_BRAKE = 0;       // timer 0
constexpr uint8_t PWM_CHANNEL_MOTOR_LEFT = 2;  // timer 1, ENA
constexpr uint8_t PWM_CHANNEL_MOTOR_RIGHT = 4; // timer 2, ENB
// 5 kHz is far above the rate the eye can follow, so the light looks steady
// rather than flickering. 8 bits gives 256 steps, which is finer than anything
// needed to control motor speed.
constexpr uint32_t PWM_FREQUENCY_HZ = 5000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
// Derived from the resolution so the two can never drift apart.
constexpr uint32_t PWM_MAX_DUTY =
    (1UL << PWM_RESOLUTION_BITS) - 1; // UL unsigned long

// --- Timing ---
constexpr unsigned long BRAKE_STEP_INTERVAL_MS = 1000;
constexpr unsigned long LINE_PRINT_INTERVAL_MS = 200;
constexpr unsigned long DISTANCE_READ_INTERVAL_MS = 100;

// --- Full-power motor check (TEMPORARY) ---
// All four motors forward together at full duty for one fixed burst, then
// stopped. The earlier version stepped through one side at a time to establish
// which way each turns; that is settled, so what is left to watch for is
// whether all four reach full speed together and whether the driver heats.
//
// One shot rather than a repeating cycle. At full duty a sequence that restarts
// on its own is a rover that spins up when nobody is expecting it to.
//
// It runs from power-up, which means it also runs after every upload, because
// flashing resets the board. Lift the wheels clear before connecting USB.
constexpr uint8_t MOTOR_TEST_PERCENT = 100;
constexpr int16_t MOTOR_TEST_DUTY =
    static_cast<int16_t>((MOTOR_TEST_PERCENT * PWM_MAX_DUTY) / 100);
constexpr unsigned long MOTOR_TEST_RUN_MS = 5000;

// --- Buzzer check (TEMPORARY) ---
// Long enough to be unmistakable in a quiet room, short enough not to be a
// nuisance at every boot.
constexpr unsigned long BUZZER_TEST_MS = 300;

// Chosen to sit above the 1-20 ms a tactile switch typically bounces for, and
// below the 100-150 ms gap between even fast human presses, so genuine presses
// still get through while the bounce does not.
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;

// --- Serial ---
constexpr unsigned long SERIAL_BAUD = 115200;

// TEMPORARY — set back to true when the line sensors have been read.
// Silences every repeating line except the line sensors, so their readings can
// be watched without four other tasks scrolling them off the screen. Only the
// printing is suppressed: the brake light, the button, the motor sequence and
// the distance measurement all still run exactly as before, so the loop timing
// stays representative. One-off messages at boot are left alone — they fire
// once and they are how you know the board actually restarted.
constexpr bool SERIAL_VERBOSE = true;

// --- Wi-Fi access point ---
// The rover creates its own network instead of joining an existing one. A
// demonstration cannot depend on infrastructure nobody here controls:
// institutional networks often need a login through a browser, hide their
// password, or isolate connected devices from one another, and any of those
// takes the phone control down at the worst possible moment. An access point
// of its own works in any room, and always answers at the same address.
//
// Phase 8 adds Station mode alongside this one, so the cloud reporting has a
// route to the internet. That is an addition rather than a replacement — the
// phone keeps talking to the rover directly either way.
constexpr char WIFI_AP_SSID[] = "Saferover";

// The passphrase is in src/secrets.h, which is not committed: this repository
// is public. WPA2 refuses anything shorter than eight characters, and the
// access point then fails to come up rather than falling back to an open
// network — part of why the result is reported explicitly below.

// One device at a time. Two operators sending driving commands at once is a
// genuinely unsafe state: one brakes while the other accelerates, and the
// rover obeys whichever packet landed last. Capping it in the radio means the
// command code never has to arbitrate between them, because the second phone
// cannot associate in the first place.
constexpr uint8_t WIFI_AP_MAX_CLIENTS = 1;

// softAP() takes the channel and the hidden flag positionally ahead of the
// client limit, so both have to be given in order to reach it. Channel 1 is
// the default and there is no reason yet to move off it. The SSID stays
// broadcast: hiding it is not a security measure, only an inconvenience.
constexpr uint8_t WIFI_AP_CHANNEL = 1;
constexpr bool WIFI_AP_HIDDEN = false;

// Brightness steps the brake light cycles through, as percentages.
constexpr uint8_t BRAKE_DUTY_PERCENTS[] = {25, 50, 75, 100};
constexpr size_t BRAKE_DUTY_STEPS =
    sizeof(BRAKE_DUTY_PERCENTS) / sizeof(BRAKE_DUTY_PERCENTS[0]);

// Store when the last step happened rather than when the next one is due:
// unsigned subtraction stays correct when millis() rolls over (~49 days).
unsigned long lastBrakeStep = 0;
size_t brakeDutyIndex = 0;

// Previous button reading, so the loop can report changes instead of flooding
// the serial line on every pass while the button is held down.
bool lastButtonPressed = false;
unsigned long lastButtonChange = 0;

unsigned long lastLinePrint = 0;
unsigned long lastDistanceRead = 0;

// Set when the burst starts at the end of setup(). The flag is what makes the
// stop happen exactly once instead of on every pass afterwards.
unsigned long motorTestStart = 0;
bool motorTestRunning = false;

// Percentages are the readable unit; the hardware wants raw counts.
static uint32_t dutyFromPercent(uint8_t percent) {
  return (static_cast<uint32_t>(percent) * PWM_MAX_DUTY) / 100;
}

static void applyBrakeDuty(size_t index) {
  const uint8_t percent = BRAKE_DUTY_PERCENTS[index];
  ledcWrite(PIN_BRAKE_LIGHT, dutyFromPercent(percent));

  if (SERIAL_VERBOSE) {
    Serial.print("BRAKE LIGHT DUTY ");
    Serial.print(percent);
    Serial.println("%");
  }
}

// Sets both sides at once. Each argument carries speed in its magnitude and
// direction in its sign, so one number per side says everything about that
// side.
//
// The two direction pins of a side are driven to opposite levels; which pattern
// means forward depends on which way round the motor leads were connected, and
// that is exactly what the bench sequence below is for. Setting both pins to
// the same level would brake the side rather than turn it, which is why they
// are always written as a pair.
static void drive(int16_t left, int16_t right) {
  const bool leftForward = (left >= 0);
  digitalWrite(PIN_MOTOR_IN1, leftForward ? HIGH : LOW);
  digitalWrite(PIN_MOTOR_IN2, leftForward ? LOW : HIGH);

  const bool rightForward = (right >= 0);
  digitalWrite(PIN_MOTOR_IN3, rightForward ? HIGH : LOW);
  digitalWrite(PIN_MOTOR_IN4, rightForward ? LOW : HIGH);

  // The sign has been consumed by the direction pins, so only the magnitude is
  // left for the speed. abs() on the full negative range of int16_t would
  // overflow, but these values are bounded by PWM_MAX_DUTY long before that.
  const uint32_t leftDuty = static_cast<uint32_t>(leftForward ? left : -left);
  const uint32_t rightDuty =
      static_cast<uint32_t>(rightForward ? right : -right);

  ledcWrite(PIN_MOTOR_ENA, min(leftDuty, PWM_MAX_DUTY));
  ledcWrite(PIN_MOTOR_ENB, min(rightDuty, PWM_MAX_DUTY));
}

// One reader for both sensors — they are the same part on the same ADC, so the
// pin is the only thing that differs.
static uint16_t readLineAveraged(uint8_t pin) {
  uint32_t total = 0;

  for (uint8_t i = 0; i < LINE_SENSOR_SAMPLES; i++) {
    total += analogRead(pin);
  }

  return static_cast<uint16_t>(total / LINE_SENSOR_SAMPLES);
}

// Runs one measurement and returns the width of the ECHO pulse in microseconds,
// or 0 when nothing came back before the timeout.
//
// delayMicroseconds appears here and nowhere else in the file. It is not a
// scheduling delay: the 10 us pulse *is* the signal the module waits for, and
// holding the line is the only way to produce it. At 10 us the cost is far
// below anything the loop can notice.
static unsigned long readEchoDuration() {
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  delayMicroseconds(TRIG_SETTLE_US);

  digitalWrite(PIN_ULTRASONIC_TRIG, HIGH);
  delayMicroseconds(TRIG_PULSE_US);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);

  return pulseIn(PIN_ULTRASONIC_ECHO, HIGH, ECHO_TIMEOUT_US);
}

// Takes DISTANCE_SAMPLES measurements and returns the median of the ones that
// came back, or 0 when too few of them did.
//
// The delay between samples is part of the measuring protocol, in the same
// sense as the microsecond delays inside the trigger pulse: a burst fired
// before the previous one has died away measures the old echo, not the new one.
static unsigned long readEchoMedian() {
  unsigned long valid[DISTANCE_SAMPLES];
  uint8_t validCount = 0;

  for (uint8_t i = 0; i < DISTANCE_SAMPLES; i++) {
    if (i > 0) {
      delay(SAMPLE_SPACING_MS);
    }

    const unsigned long sample = readEchoDuration();

    // A timeout carries no distance, so it is dropped rather than sorted with
    // the rest — a 0 would sink to the bottom and drag the median down with it.
    if (sample != 0) {
      valid[validCount++] = sample;
    }
  }

  // With most of the samples gone, reporting a distance from the one that
  // survived would be a guess presented as a measurement. A safety system is
  // better off declaring that it has no information than stating a wrong value
  // confidently — the caller already knows how to handle "no reading", and the
  // existing 0 return is exactly that signal.
  if (validCount < DISTANCE_MIN_VALID_SAMPLES) {
    return 0;
  }

  // Never more than three elements, so a plain insertion sort is the clearest
  // thing that works; anything cleverer would cost readability for no gain.
  for (uint8_t i = 1; i < validCount; i++) {
    const unsigned long key = valid[i];
    uint8_t j = i;

    while (j > 0 && valid[j - 1] > key) {
      valid[j] = valid[j - 1];
      j--;
    }

    valid[j] = key;
  }

  // An odd count has a real middle. An even one does not, and the tie-break
  // here is the lower of the two: averaging them would let a bad sample pull
  // the result, which is the whole thing the median was chosen to avoid, and of
  // two candidate distances the nearer one is the safe one for a brake to act
  // on.
  const uint8_t medianIndex =
      (validCount % 2 == 1) ? (validCount / 2) : (validCount / 2 - 1);

  return valid[medianIndex];
}

// The pulse times a round trip: the burst travels out to the obstacle and back
// again, so the distance to the obstacle is half of what the sound covered.
static float distanceFromDuration(unsigned long durationUs) {
  return (durationUs * SOUND_SPEED_CM_PER_US) / 2.0f;
}

// Draws the one-off screen that proves the panel works and that the configured
// resolution matches the physical one.
//
// Every call below writes into a pixel buffer held in the ESP32's own RAM —
// nothing reaches the panel until display() pushes that whole buffer out over
// I2C. display() is therefore the expensive operation, and the only one whose
// cost scales with the bus rather than the CPU. In the finished firmware it
// gets called when the content actually changes, never once per loop pass.
static void showTestScreen() {
  display.clearDisplay();

  constexpr uint16_t FOREGROUND = SSD1306_WHITE;

  // The border is the resolution test. It sits on the outermost pixels on all
  // four sides, so if the panel is really a different size the frame will not
  // line up with its physical edges — cropped, or floating short of them. Text
  // alone would still look plausible on a wrongly sized buffer.
  display.drawRect(0, 0, OLED_WIDTH, OLED_HEIGHT, FOREGROUND);

  display.setTextSize(1);
  display.setTextColor(FOREGROUND);

  display.setCursor(OLED_TEXT_LEFT, OLED_TEXT_TOP);
  display.print("SafeRover");

  display.setCursor(OLED_TEXT_LEFT, OLED_TEXT_TOP + OLED_LINE_SPACING);
  display.print("phase 2 - bench");

  // The configured resolution is printed as well as drawn. If the border looks
  // wrong, this says what the firmware believed the panel was.
  display.setCursor(OLED_TEXT_LEFT, OLED_TEXT_TOP + 2 * OLED_LINE_SPACING);
  display.print(OLED_WIDTH);
  display.print("x");
  display.print(OLED_HEIGHT);

  display.display();
}

// Brings up the access point and reports where to find it. Nothing here waits:
// softAP() returns with the interface already up and addressed, so there is no
// delay to justify.
//
// From here on the radio is running, which is where the ADC2 restriction noted
// beside the line sensors stops being theoretical. Both of them are on ADC1,
// so nothing about them changes.
static void startAccessPoint() {
  // AP only. WIFI_AP_STA would also raise the station interface, which has no
  // network to join yet and would spend its time scanning for one.
  WiFi.mode(WIFI_AP);

  const bool started =
      WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD, WIFI_AP_CHANNEL,
                  WIFI_AP_HIDDEN, WIFI_AP_MAX_CLIENTS);

  // Deliberately not behind SERIAL_VERBOSE. These print once at boot, and the
  // address is the only way to know what to point a browser at.
  if (!started) {
    // Reported and then let go, the same as the display above: a rover that
    // stops dead because a radio did not come up is worse than one that still
    // drives without it.
    Serial.println("WIFI AP FAILED - rover continues without it");
    return;
  }

  Serial.print("WIFI AP up  SSID=");
  Serial.println(WIFI_AP_SSID);
  Serial.print("WIFI AP IP  ");
  Serial.println(WiFi.softAPIP());
}

void setup() {
  // First, before anything else: the buzzer sounds while its pin is undriven,
  // so every instruction that runs before this one is an instruction spent
  // making noise. Serial.begin() alone is long enough to hear.
  //
  // The output latch is set before the pin becomes an output, so it drives the
  // silent level as it switches rather than passing through the active one.
  digitalWrite(PIN_BUZZER, BUZZER_SILENT);
  pinMode(PIN_BUZZER, OUTPUT);

  Serial.begin(SERIAL_BAUD);

  // Binding the channel explicitly rather than letting the core pick one, so
  // the allocation stays predictable once the motor channels are added.
  ledcAttachChannel(PIN_BRAKE_LIGHT, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS,
                    PWM_CHANNEL_BRAKE);

  // Lit for as long as the board is powered, so it also shows at a glance
  // that the firmware actually reached setup().
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, HIGH);

  // INPUT_PULLUP switches on a resistor inside the ESP32 that holds the pin at
  // 3V3 while the button is open. The other leg of the button goes to GND, so
  // closing it drags the pin down and a press reads LOW. That internal resistor
  // is why no external one is wired.
  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);

  // The two enable pins get the same frequency and resolution as the brake
  // light for now, but they sit on their own timers, so a motor frequency can
  // be changed later without dragging the brake light along with it.
  ledcAttachChannel(PIN_MOTOR_ENA, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS,
                    PWM_CHANNEL_MOTOR_LEFT);
  ledcAttachChannel(PIN_MOTOR_ENB, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS,
                    PWM_CHANNEL_MOTOR_RIGHT);

  pinMode(PIN_MOTOR_IN1, OUTPUT);
  pinMode(PIN_MOTOR_IN2, OUTPUT);
  pinMode(PIN_MOTOR_IN3, OUTPUT);
  pinMode(PIN_MOTOR_IN4, OUTPUT);

  // Stopped before anything else runs. The direction pins power up in an
  // undefined state, and four motors on a chassis with no caster will happily
  // drive off a bench while the rest of setup() is still going.
  drive(0, 0);

  // TRIG is driven by us, ECHO is read. Parking TRIG LOW here means the module
  // is not looking at a half-raised line before the first measurement runs.
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);

  analogReadResolution(ADC_RESOLUTION_BITS);
  analogSetPinAttenuation(PIN_LINE_SENSOR_LEFT, ADC_11db);
  analogSetPinAttenuation(PIN_LINE_SENSOR_RIGHT, ADC_11db);

  // The bus is opened here, with the pins named, rather than left to the
  // library. The last argument tells begin() not to call Wire.begin() itself:
  // it would re-open the bus on the core's default pins, which would only work
  // by coincidence.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // begin() returns false when the panel does not answer. Checking it turns a
  // silent dead screen into a named fault — an unchecked init failure and a
  // display that simply never lit look identical from the outside, and that is
  // one unknown too many. A failure here is reported and then let go: the rover
  // keeps running without its screen rather than stopping dead.
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false)) {
    showTestScreen();
  } else {
    Serial.print("OLED init FAILED at 0x");
    Serial.println(OLED_ADDRESS, HEX);
    Serial.println("bus answered during the scan, so suspect the controller "
                   "chip - try Adafruit_SH110X");
  }

  startAccessPoint();

  Serial.print("SafeRover boot OK - brake light PWM on GPIO ");
  Serial.println(PIN_BRAKE_LIGHT);

  applyBrakeDuty(brakeDutyIndex);

  // TEMPORARY — one beep, to prove the buzzer works at all. It is the only part
  // in the project that has never been tested, on the bench or on the vehicle,
  // and nothing else in the firmware drives this pin until the AEB stage in
  // phase 5. Without this it would stay unverified until the moment it is
  // needed for something real. Remove it when AEB takes the pin over.
  //
  // delay() is allowed here. The rule against it applies to the main loop,
  // where blocking means the sensors stop being read; setup() runs once before
  // that loop starts and holds nothing up.
  if (SERIAL_VERBOSE) {
    Serial.print("BUZZER TEST  ");
    Serial.print(BUZZER_TEST_MS);
    Serial.println(" ms beep on GPIO 4");
  }

  digitalWrite(PIN_BUZZER, BUZZER_SOUND);
  delay(BUZZER_TEST_MS);
  digitalWrite(PIN_BUZZER, BUZZER_SILENT);

  // TEMPORARY — the full-power burst, started last so that nothing else in
  // setup() eats into the run time that loop() is now timing.
  Serial.print("MOTOR TEST  all four forward at ");
  Serial.print(MOTOR_TEST_PERCENT);
  Serial.print("% for ");
  Serial.print(MOTOR_TEST_RUN_MS);
  Serial.println(" ms");

  motorTestStart = millis();
  motorTestRunning = true;
  drive(MOTOR_TEST_DUTY, MOTOR_TEST_DUTY);
}

void loop() {
  const unsigned long now = millis();

  // delay() is banned here by project convention: it blocks the whole loop, and
  // later phases must keep polling the ultrasonic sensor while this steps.
  if (now - lastBrakeStep >= BRAKE_STEP_INTERVAL_MS) {
    lastBrakeStep = now;

    brakeDutyIndex = (brakeDutyIndex + 1) % BRAKE_DUTY_STEPS;
    applyBrakeDuty(brakeDutyIndex);
  }

  // Report transitions only, and only once the debounce window has passed. The
  // metal contacts bounce for a few milliseconds on every open and close, which
  // the loop is fast enough to read as a burst of separate edges.
  const bool buttonPressed = (digitalRead(PIN_MODE_BUTTON) == LOW);
  if (buttonPressed != lastButtonPressed &&
      now - lastButtonChange >= BUTTON_DEBOUNCE_MS) {
    lastButtonChange = now;
    lastButtonPressed = buttonPressed;

    if (SERIAL_VERBOSE) {
      Serial.println(buttonPressed ? "BUTTON PRESSED" : "BUTTON RELEASED");
    }
  }

  // Data collection only: read and report. No thresholds, no decisions.
  if (now - lastLinePrint >= LINE_PRINT_INTERVAL_MS) {
    lastLinePrint = now;

    const uint16_t left = readLineAveraged(PIN_LINE_SENSOR_LEFT);
    const uint16_t right = readLineAveraged(PIN_LINE_SENSOR_RIGHT);

    // A bench aid, not a control input. Both sensors read white while the rover
    // is inside the lane, so this difference rests at a non-zero constant there
    // and only moves once one of them reaches a stripe. How the LKA turns the
    // two readings into an error signal is still open.
    const int16_t delta =
        static_cast<int16_t>(left) - static_cast<int16_t>(right);

    Serial.print("LINE  L=");
    Serial.print(left);
    Serial.print("  R=");
    Serial.print(right);
    Serial.print("  delta=");
    Serial.println(delta);
  }

  // TEMPORARY — ends the full-power burst that setup() started. Subtracting the
  // two millis() values rather than comparing them keeps this correct across
  // the counter's rollover, the same as every other timer in this loop.
  if (motorTestRunning && now - motorTestStart >= MOTOR_TEST_RUN_MS) {
    drive(0, 0);
    motorTestRunning = false;

    Serial.println("MOTOR TEST  burst finished, motors stopped");
  }

  // Median-filtered now that the raw noise has been characterised on the bench:
  // the unfiltered signal was steady to within a tenth of a percent against a
  // fixed target, so the filter is not there to smooth a wobble. It is there
  // for the occasional sample that comes back badly wrong, which is what a
  // moving, vibrating vehicle will produce.
  if (now - lastDistanceRead >= DISTANCE_READ_INTERVAL_MS) {
    lastDistanceRead = now;

    // Both values get printed: the pulse says whether a bad number came from
    // the sensor or from the conversion below it.
    const unsigned long echoDuration = readEchoMedian();

    if (SERIAL_VERBOSE) {
      Serial.print("DIST  echo=");
      Serial.print(echoDuration);
      Serial.print("us  ");

      // A timeout is not a distance. Converting the 0 would print 0.0 cm, which
      // reads exactly like an obstacle pressed against the sensor — the most
      // dangerous possible misreading once this drives the brakes.
      if (echoDuration == 0) {
        Serial.println("d=--  no echo");
      } else {
        Serial.print("d=");
        Serial.print(distanceFromDuration(echoDuration), 1);
        Serial.println("cm");
      }
    }
  }
}
