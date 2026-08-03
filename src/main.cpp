#include <Arduino.h>

// Phase 1 — brake light on PWM, status LED and mode button.
// Phase 2 — two line sensors on GPIO 34 and 35, read and reported together with
// the difference between them. Their reference values have been measured on the
// bench; see the comment beside the pin definitions. Phase 2 also brings up the
// HC-SR04 on GPIO 5 and 18, which reports the echo pulse next to the distance it
// converts to, median-filtered over three samples. Still data collection only:
// there are no thresholds and no driving decisions in this file yet.
// Pins follow the wiring contract in README.md. GPIO 16/17 from the original
// plan are not broken out on this 30-pin board, so the brake light is on 19
// and the status LED on 13.
//
// The PWM here runs on the brake light only because it is already wired and the
// result is visible. In the finished rover the brake light is on/off, and this
// same mechanism drives the L298N enable pins for motor speed.

// --- Pin assignments ---
constexpr uint8_t PIN_BRAKE_LIGHT = 19;
constexpr uint8_t PIN_STATUS_LED = 13;
constexpr uint8_t PIN_MODE_BUTTON = 23;

// Both line sensors sit on ADC1. ADC2 cannot be used while Wi-Fi is running
// because it shares hardware with the radio, so every analog sensor in this
// project stays on GPIO 32-39. Only the modules' analog outputs are wired: the
// LKA needs a continuous value for its proportional controller, not a binary DO
// answer.
//
// Measured on the bench at the working height of 3.5 cm, averaged over 16
// samples. The direction is the opposite of the intuitive one — a HIGH value
// means a DARK surface, so a high reading is the sensor sitting over the line.
//
//   both over white    L ~60     R ~70     delta ~-10
//   both over black    L ~4028   R ~4006   delta ~+20
//   one over each                          delta ~4030
//
// Noise on a steady reading is about +/-7. The two sensors track each other to
// within 20 counts over the same surface — under 1% of range — so neither needs
// its own correction factor.
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

// --- Ultrasonic filtering ---
// Each measurement takes three samples and keeps the middle one. A median rather
// than an average, because the two fail differently: an average is dragged toward
// a bad sample in proportion to how bad it is, while a median steps over it — one
// wild value out of three cannot end up in the middle. That distinction matters
// because ultrasonic errors have exactly that shape. A missed echo, or one picked
// up off a wall at an angle, lands far from the truth rather than slightly off
// it, so a single one visibly moves an average and does not move a median at all.
constexpr uint8_t DISTANCE_SAMPLES = 3;

// How many of those samples have to come back with an echo before the result
// counts as a measurement at all.
constexpr uint8_t DISTANCE_MIN_VALID_SAMPLES = 2;

// The bursts need spacing, or the next measurement hears the tail of the previous
// one still bouncing around the room and reads it as a much closer object.
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
constexpr uint8_t PWM_CHANNEL_BRAKE = 0;                        // timer 0
[[maybe_unused]] constexpr uint8_t PWM_CHANNEL_MOTOR_LEFT = 2;  // timer 1, ENA
[[maybe_unused]] constexpr uint8_t PWM_CHANNEL_MOTOR_RIGHT = 4; // timer 2, ENB
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

// Chosen to sit above the 1-20 ms a tactile switch typically bounces for, and
// below the 100-150 ms gap between even fast human presses, so genuine presses
// still get through while the bounce does not.
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;

// --- Serial ---
constexpr unsigned long SERIAL_BAUD = 115200;

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

// Percentages are the readable unit; the hardware wants raw counts.
static uint32_t dutyFromPercent(uint8_t percent) {
  return (static_cast<uint32_t>(percent) * PWM_MAX_DUTY) / 100;
}

static void applyBrakeDuty(size_t index) {
  const uint8_t percent = BRAKE_DUTY_PERCENTS[index];
  ledcWrite(PIN_BRAKE_LIGHT, dutyFromPercent(percent));

  Serial.print("BRAKE LIGHT DUTY ");
  Serial.print(percent);
  Serial.println("%");
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
// The delay between samples is part of the measuring protocol, in the same sense
// as the microsecond delays inside the trigger pulse: a burst fired before the
// previous one has died away measures the old echo, not the new one.
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

  // An odd count has a real middle. An even one does not, and the tie-break here
  // is the lower of the two: averaging them would let a bad sample pull the
  // result, which is the whole thing the median was chosen to avoid, and of two
  // candidate distances the nearer one is the safe one for a brake to act on.
  const uint8_t medianIndex =
      (validCount % 2 == 1) ? (validCount / 2) : (validCount / 2 - 1);

  return valid[medianIndex];
}

// The pulse times a round trip: the burst travels out to the obstacle and back
// again, so the distance to the obstacle is half of what the sound covered.
static float distanceFromDuration(unsigned long durationUs) {
  return (durationUs * SOUND_SPEED_CM_PER_US) / 2.0f;
}

void setup() {
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

  // TRIG is driven by us, ECHO is read. Parking TRIG LOW here means the module
  // is not looking at a half-raised line before the first measurement runs.
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);

  analogReadResolution(ADC_RESOLUTION_BITS);
  analogSetPinAttenuation(PIN_LINE_SENSOR_LEFT, ADC_11db);
  analogSetPinAttenuation(PIN_LINE_SENSOR_RIGHT, ADC_11db);

  Serial.print("SafeRover boot OK - brake light PWM on GPIO ");
  Serial.println(PIN_BRAKE_LIGHT);

  applyBrakeDuty(brakeDutyIndex);
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

    Serial.println(buttonPressed ? "BUTTON PRESSED" : "BUTTON RELEASED");
  }

  // Data collection only: read and report. No thresholds, no decisions.
  if (now - lastLinePrint >= LINE_PRINT_INTERVAL_MS) {
    lastLinePrint = now;

    const uint16_t left = readLineAveraged(PIN_LINE_SENSOR_LEFT);
    const uint16_t right = readLineAveraged(PIN_LINE_SENSOR_RIGHT);

    // Left minus right is the error signal the LKA's P controller will act on:
    // zero means the rover is centred, and the sign says which way it drifted.
    const int16_t delta =
        static_cast<int16_t>(left) - static_cast<int16_t>(right);

    Serial.print("LINE  L=");
    Serial.print(left);
    Serial.print("  R=");
    Serial.print(right);
    Serial.print("  delta=");
    Serial.println(delta);
  }

  // Median-filtered now that the raw noise has been characterised on the bench:
  // the unfiltered signal was steady to within a tenth of a percent against a
  // fixed target, so the filter is not there to smooth a wobble. It is there for
  // the occasional sample that comes back badly wrong, which is what a moving,
  // vibrating vehicle will produce.
  if (now - lastDistanceRead >= DISTANCE_READ_INTERVAL_MS) {
    lastDistanceRead = now;

    // Both values get printed: the pulse says whether a bad number came from the
    // sensor or from the conversion below it.
    const unsigned long echoDuration = readEchoMedian();

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
