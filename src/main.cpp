#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include "secrets.h"
#include "web_page.h"

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
constexpr unsigned long LINE_PRINT_INTERVAL_MS = 200;
constexpr unsigned long DISTANCE_READ_INTERVAL_MS = 100;

// --- Buzzer self test ---
// Long enough to be unmistakable in a quiet room, short enough not to be a
// nuisance at every boot.
constexpr unsigned long BUZZER_TEST_MS = 300;

// Chosen to sit above the 1-20 ms a tactile switch typically bounces for, and
// below the 100-150 ms gap between even fast human presses, so genuine presses
// still get through while the bounce does not.
constexpr unsigned long BUTTON_DEBOUNCE_MS = 50;

// --- Serial ---
constexpr unsigned long SERIAL_BAUD = 115200;

// One switch per repeating report, rather than a single verbose flag.
//
// The single flag was written when the line sensors were the only thing being
// watched, and it stopped fitting as soon as more than one subsystem was
// printing. At any moment one of them is under test and the rest are noise, and
// a single boolean cannot say which one. With the driving commands added there
// are four repeating reports running at once - roughly twenty-four lines a
// second - and reading any one of them meant reading all of them.
//
// Only the printing is suppressed. Every task still runs at its own rate, so
// the loop timing stays representative of the real thing.
//
// Events are deliberately absent from this list and are never silenced: a
// rejected command, the watchdog firing, a failed init, the addresses reported
// at boot. Those are not reports that repeat, they are things that happened.
constexpr bool LOG_BUTTON = false;
constexpr bool LOG_LINE = false;
constexpr bool LOG_DISTANCE = false;
constexpr bool LOG_DRIVE = true;

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

// --- Web server ---
// Port 80 is the one a browser assumes when none is given, so the address is
// the plain IP with nothing after it. Named rather than left as a literal
// because it also appears in the URL printed at boot, and the two must not
// drift apart.
constexpr uint16_t WEB_SERVER_PORT = 80;

// How often the browser asks for fresh numbers. Half a second is quick enough
// that the values feel live and slow enough that answering them stays cheap:
// every request is time taken from the loop, and in phase 5 that loop runs the
// AEB.
constexpr unsigned long WEB_POLL_INTERVAL_MS = 500;

// How often the loop services the socket. These two constants are coupled, and
// this one has to stay far below the one above. handleClient() takes at most
// ONE pending request per call, so if the two rates were close, any hesitation
// would leave a request waiting, then a second would arrive on top of it, and
// the backlog would grow instead of draining. A factor of twenty-five leaves
// room that does not accumulate.
constexpr unsigned long WEB_HANDLE_INTERVAL_MS = 20;

// Built from the constant above, the same way the display object is built from
// the panel's. Synchronous by design: it does no work of its own until the loop
// calls handleClient(), so nothing about it runs behind the scheduler's back.
WebServer server(WEB_SERVER_PORT);

// --- Driving commands ---
// How long the rover accepts silence before stopping itself.
//
// Nothing in this system decays on its own. The commanded values below hold
// whatever arrived last, and LEDC is a peripheral: written once, it keeps
// producing that waveform with no further work from the CPU. So there is
// nothing anywhere - not in software, not in hardware - that would return the
// motors to zero on its own. A rover whose operator vanished would drive until
// the battery ran out, because the last command was never withdrawn. Silence on
// a control channel is not consent to keep going.
//
// The value is a balance. Too short and an ordinary network hiccup stops the
// rover mid-drive; too long and it keeps driving after the phone is gone. The
// page sends several times faster than this, so three consecutive messages have
// to be lost before it stops by mistake.
//
// In centimetres, now that ground speed has been measured on the floor: half a
// second is about 42 cm at full power and about 16 cm at the minimum. Whether
// 42 cm is acceptable on a small track is a separate decision that has not been
// taken, so the value is left as it was.
constexpr unsigned long COMMAND_TIMEOUT_MS = 500;

// How often the page repeats the current command while a button is held.
//
// This is not what makes the controls feel responsive. Pressing and releasing
// each send immediately, so the rover reacts on the edge of the gesture rather
// than waiting for the next tick. The only job of this rate is to keep proving
// the channel is alive, which is why its ratio to the timeout above is the only
// thing about it that matters: three of these have to go missing in a row
// before the rover stops by mistake.
constexpr unsigned long COMMAND_SEND_INTERVAL_MS = 120;

// The slowest setting worth offering, as a percentage of full power. Below some
// threshold the motors do not turn at all - the torque never overcomes static
// friction and the gearbox, so they sit drawing current and whining. That
// threshold belongs to these motors and this chassis; it is physical rather
// than a preference.
//
// A percentage rather than a raw duty, so that the page carries no number
// derived from the PWM resolution. dutyFromPercent converts, here in the
// firmware, which is the only side allowed to know how the motors are driven.
// It also reads more honestly: a slider whose left end is 62 does not look like
// a stop, and one whose left end is zero would.
//
// Measured, on the floor and on the rover's own supply, by lowering the slider
// until the wheels stopped turning and then coming back up. It replaces a
// declared guess of 35, which left everything from 35 to 61 as a dead band that
// drew current and produced no motion - the exact condition this constant
// exists to keep out of. The speed curve behind it is in the journal.
//
// Checked on both sides. The slider will not go below it, and the firmware
// refuses anything below it that arrives anyway. Neither trusts the other.
//
// It is not a way to stop. At the minimum the rover crawls, it does not halt.
// Releasing the button stops it, and the watchdog stops it when nothing
// releases anything.
constexpr uint8_t DRIVE_MIN_PERCENT = 62;

// Where the slider sits when the page loads. Deliberately low: the first runs
// on a floor happen indoors, and a rover that starts at full power finds a
// wall.
//
// Kept clear of the floor rather than sitting on it. 62 is where the wheels
// were seen to start, which makes it an edge rather than a safe setting, and a
// default with no margin above it is one that depends on the surface being the
// one it was measured on. 75 is the middle of the three runs that were timed,
// about 50 cm/s, and still slow indoors.
constexpr uint8_t DRIVE_START_PERCENT = 75;

// Nothing checked the relation between these two, and the default had been left
// below the floor. What a browser does with a range value under its minimum was
// not tested and is beside the point: the page would carry a setting this
// firmware is required to reject, which is an invalid state whatever it looks
// like on screen. Cheaper to make it a compile error than to notice it later.
static_assert(DRIVE_START_PERCENT >= DRIVE_MIN_PERCENT,
              "slider default must not sit below the floor");

// --- AEB (autonomous emergency braking) ---
// Three stages by distance to whatever is in front.
//
// NONE OF THESE IS MEASURED. At 84.4 cm/s the rover covers about 8.4 cm between
// two distance reads, which is the floor under any threshold and under the
// hysteresis. The braking distance itself has never been measured, so whether
// 20 cm leaves room is open. Stop also stays clear of the sensor's blind zone
// under ~2 cm, where the transmitter still rings as the echo arrives.
constexpr float AEB_WARN_CM = 60.0f;
constexpr float AEB_SLOW_CM = 35.0f;
constexpr float AEB_STOP_CM = 20.0f;

// How much further out before a stage is left again. Without it a reading
// resting on a threshold flickers across it on noise alone. Entering is
// immediate, leaving is reluctant: escalating late is a collision.
constexpr float AEB_HYSTERESIS_CM = 8.0f;

// Derived, so the three exits cannot drift out of step with the entries.
constexpr float AEB_WARN_EXIT_CM = AEB_WARN_CM + AEB_HYSTERESIS_CM;
constexpr float AEB_SLOW_EXIT_CM = AEB_SLOW_CM + AEB_HYSTERESIS_CM;
constexpr float AEB_STOP_EXIT_CM = AEB_STOP_CM + AEB_HYSTERESIS_CM;

static_assert(AEB_STOP_CM < AEB_SLOW_CM && AEB_SLOW_CM < AEB_WARN_CM,
              "AEB stages must escalate as the distance shrinks");

// An exit above the next entry would re-enter the stage above on the way out,
// leaving the rover oscillating between two stages instead of settling.
static_assert(AEB_STOP_EXIT_CM < AEB_SLOW_CM && AEB_SLOW_EXIT_CM < AEB_WARN_CM,
              "hysteresis must not reach into the next stage");

// Failed readings tolerated in a row before the silence is acted on. One missed
// echo is ordinary, and readEchoMedian already needs two of three samples, so a
// zero is a mostly failed measurement rather than one bad sample. Three is
// about 300 ms, some 25 cm at full power.
constexpr uint8_t AEB_MISSED_READS_LIMIT = 3;

// The ceiling the Slow stage imposes, as a percentage of full power. It has to
// stay above DRIVE_MIN_PERCENT: under the floor the rover does not slow down,
// it stops, and a stage that quietly becomes the stage below it is worse than
// no stage at all.
constexpr uint8_t AEB_SLOW_PERCENT = 70;
static_assert(AEB_SLOW_PERCENT >= DRIVE_MIN_PERCENT,
              "the slow stage must still be able to move the rover");

// How old the last reading may be before the stage stops being trusted.
// Derived from the read interval, so it follows if that changes. Three
// intervals means the loop has stopped measuring rather than run slightly late.
constexpr unsigned long AEB_READING_MAX_AGE_MS = DISTANCE_READ_INTERVAL_MS * 3;

// One value, not a flag per stage: with four booleans there is a state where
// Warn and Stop are both set and something has to decide which wins. The order
// is compared directly - larger is more severe - so keep it.
enum class AebStage : uint8_t { Clear, Warn, Slow, Stop };

// What a stop means at the motors. Coasting drops the duty to zero and lets the
// wheels roll on; braking shorts each motor through the bridge and holds it.
enum class StopMode : uint8_t { Coast, Brake };

// Previous button reading, so the loop can report changes instead of flooding
// the serial line on every pass while the button is held down.
bool lastButtonPressed = false;
unsigned long lastButtonChange = 0;

unsigned long lastLinePrint = 0;
unsigned long lastDistanceRead = 0;
unsigned long lastWebHandle = 0;

// The last readings the loop took, held at file scope so a request handler can
// report them without measuring anything itself.
//
// That restriction is the point. Reading a sensor inside a handler would let a
// browser decide when pulseIn fires, and two ultrasonic bursts close together
// each measure the other's echo - which is what SAMPLE_SPACING_MS exists to
// prevent. A phone refreshing a page could then corrupt a distance reading, and
// from phase 5 that reading works the brakes. Measuring stays the loop's job;
// the page only reports what the loop already found.
//
// echoDurationValue keeps the meaning zero already carries everywhere else in
// this file: no echo came back, which is not a distance. The page inherits that
// convention rather than inventing one.
uint16_t lineLeftValue = 0;
uint16_t lineRightValue = 0;
unsigned long echoDurationValue = 0;

// What the operator has asked for, in the form drive() already takes: sign is
// direction, magnitude is duty.
//
// Held here rather than acted on where they arrive. A request handler must not
// be the thing that writes to motors; the loop is, once per pass, at a moment
// of its own choosing. That leaves a single writer at a known point - which is
// also where the AEB will intercept in phase 5, without touching any of the
// code that decides where to go.
//
// commandTimedOut starts true, meaning no command has ever arrived rather than
// one arrived just now. The rover is quiet after a reset by decision rather
// than by these values happening to be zero, and while the flag is true it does
// not matter what millis() read at startup.
int16_t commandedLeft = 0;
int16_t commandedRight = 0;
unsigned long lastCommandMs = 0;
bool commandTimedOut = true;

// Starts Clear because nothing has been measured yet, not because the road is
// known to be empty. The first reading, 100 ms in, replaces it.
AebStage aebStage = AebStage::Clear;
uint8_t aebMissedReads = 0;

// The last distance that actually came back, kept because a missing echo means
// different things depending on what preceded it. Started just outside the warn
// threshold, which is the assumption made before any reading exists: open road.
float aebLastValidCm = AEB_WARN_CM + 1.0f;

// Percentages are the readable unit; the hardware wants raw counts.
static uint32_t dutyFromPercent(uint8_t percent) {
  return (static_cast<uint32_t>(percent) * PWM_MAX_DUTY) / 100;
}

// Drives the two warning outputs from the current stage.
//
// Both are gated on the operator actually asking for movement. A rover parked
// near a wall with nobody touching anything is not braking and has nothing to
// announce, and a warning that never stops carries no information - real
// parking sensors behave the same way.
//
// The brake light follows the stages that actually slow the rover down rather
// than the one that only warns, the same as a car: the lamps mean deceleration,
// not concern. The buzzer covers all three, because a warning nobody can see
// from behind still has to reach the operator.
//
// The stage arrives as an argument rather than being read from the global, so
// the indicators report the stage that was actually enforced - including one
// forced by a stale reading, which the global does not know about.
static void applyAebOutputs(bool motionRequested, AebStage stage) {
  const bool sounding = motionRequested && stage != AebStage::Clear;
  const bool braking =
      motionRequested && (stage == AebStage::Slow || stage == AebStage::Stop);

  digitalWrite(PIN_BUZZER, sounding ? BUZZER_SOUND : BUZZER_SILENT);
  ledcWrite(PIN_BRAKE_LIGHT, braking ? PWM_MAX_DUTY : 0);
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
//
// The third argument says what a stop means, and it has no default: every
// caller has to state it. Braking is a whole-vehicle action, so it ignores the
// two speeds rather than pretending to mix with them.
static void drive(int16_t left, int16_t right, StopMode stop) {
  if (stop == StopMode::Brake) {
    // Both direction pins of a side at the same level short that motor through
    // the bridge, and its own momentum then works against it. The enable has to
    // stay high for any of that to reach the windings: at duty zero the outputs
    // are disconnected and the wheels roll on no matter what the direction pins
    // say, which is exactly what coasting is.
    digitalWrite(PIN_MOTOR_IN1, LOW);
    digitalWrite(PIN_MOTOR_IN2, LOW);
    digitalWrite(PIN_MOTOR_IN3, LOW);
    digitalWrite(PIN_MOTOR_IN4, LOW);

    ledcWrite(PIN_MOTOR_ENA, PWM_MAX_DUTY);
    ledcWrite(PIN_MOTOR_ENB, PWM_MAX_DUTY);
    return;
  }

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

// The one door to the motors, and the only thing any driving decision is
// allowed to call. Manual control already comes through here and the lane
// keeping will too, so the AEB intercepts every one of them without a line of
// the code that decides where to go being touched. A safety layer that
// navigation can route around is not a safety layer.
//
// The one exception is the drive() in setup(). That is hardware initialisation
// rather than a driving decision - the direction pins come up undefined - and
// at that moment there is no reading to reason about.
static void safeDrive(int16_t left, int16_t right) {
  // Its own clock rather than one handed in: a caller cannot pass a stale
  // timestamp and talk this layer out of the check below.
  const bool readingStale =
      (millis() - lastDistanceRead) > AEB_READING_MAX_AGE_MS;

  // A missing echo can honestly mean open road, and updateAebStage treats it as
  // such. A reading that has stopped arriving at all cannot mean anything but a
  // fault, and a fault in the braking layer fails towards stopping.
  const AebStage stage = readingStale ? AebStage::Stop : aebStage;

  applyAebOutputs(left != 0 || right != 0, stage);

  // Reverse and pivot turns are escapes and are never limited. A rover pinned
  // against a wall by its own safety layer, with the only commands that would
  // free it refused, is a worse failure than the one being prevented. In
  // differential steering a negative side is exactly one of those manoeuvres,
  // and the sensor only faces forward anyway.
  if (left < 0 || right < 0) {
    drive(left, right, StopMode::Coast);
    return;
  }

  if (stage == AebStage::Stop) {
    drive(0, 0, StopMode::Brake);
    return;
  }

  if (stage == AebStage::Slow) {
    // A ceiling, never an assignment. A command already slower than this passes
    // through untouched: this layer is only ever allowed to take speed away.
    const int16_t cap = static_cast<int16_t>(dutyFromPercent(AEB_SLOW_PERCENT));
    left = min(left, cap);
    right = min(right, cap);
  }

  drive(left, right, StopMode::Coast);
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

// Names the stage for the log. All four listed rather than a default, so a
// stage added later warns here instead of printing as something else.
static const char *aebStageName(AebStage stage) {
  switch (stage) {
  case AebStage::Clear:
    return "CLEAR";
  case AebStage::Warn:
    return "WARN";
  case AebStage::Slow:
    return "SLOW";
  case AebStage::Stop:
    return "STOP";
  }
  return "?";
}

// Picks the stage from the reading the loop has just taken. Called once per
// measurement rather than once per pass: the input only changes ten times a
// second. It decides and reports and does nothing else - enforcement belongs
// inside safeDrive, where no caller can route around it.
static void updateAebStage() {
  const AebStage previous = aebStage;
  float distanceCm = 0.0f;

  if (echoDurationValue == 0) {
    aebMissedReads++;

    // Under the limit the stage is held. One lost echo is the absence of
    // evidence, not evidence that anything ahead changed.
    if (aebMissedReads < AEB_MISSED_READS_LIMIT) {
      return;
    }

    // Saturated, so a long outage cannot wrap the counter and hand the
    // tolerance back from zero.
    aebMissedReads = AEB_MISSED_READS_LIMIT;

    // Sustained silence means opposite things depending on what came before it.
    // Losing the echo at three metres is open road - the module only reaches
    // about four. Losing it at fifteen is something close, possibly inside the
    // blind zone under 2 cm, which returns nothing at all.
    //
    // The hole this leaves is a sensor that dies while the road really is
    // empty: the last reading is large, the stage stays Clear, and nothing
    // brakes. Closing it would mean stopping in every open room.
    aebStage =
        (aebLastValidCm <= AEB_WARN_CM) ? AebStage::Stop : AebStage::Clear;
  } else {
    aebMissedReads = 0;
    distanceCm = distanceFromDuration(echoDurationValue);
    aebLastValidCm = distanceCm;

    AebStage byDistance = AebStage::Clear;
    if (distanceCm <= AEB_STOP_CM) {
      byDistance = AebStage::Stop;
    } else if (distanceCm <= AEB_SLOW_CM) {
      byDistance = AebStage::Slow;
    } else if (distanceCm <= AEB_WARN_CM) {
      byDistance = AebStage::Warn;
    }

    if (byDistance > aebStage) {
      // Escalation skips nothing. Something appearing inside the stop threshold
      // goes straight there rather than stepping down over three readings,
      // which at full power would be a quarter of a metre.
      aebStage = byDistance;
    } else {
      // Leaving takes one stage per reading, and only past the exit threshold,
      // so backing away from a wall takes about 300 ms to reach Clear.
      switch (aebStage) {
      case AebStage::Stop:
        if (distanceCm > AEB_STOP_EXIT_CM) {
          aebStage = AebStage::Slow;
        }
        break;
      case AebStage::Slow:
        if (distanceCm > AEB_SLOW_EXIT_CM) {
          aebStage = AebStage::Warn;
        }
        break;
      case AebStage::Warn:
        if (distanceCm > AEB_WARN_EXIT_CM) {
          aebStage = AebStage::Clear;
        }
        break;
      case AebStage::Clear:
        break;
      }
    }
  }

  // Transitions only, and behind no log switch: this is an event, like the
  // watchdog message. The distance goes with it because that is what a
  // calibration run compares against a ruler.
  if (aebStage != previous) {
    Serial.print("AEB  ");
    Serial.print(aebStageName(previous));
    Serial.print(" -> ");
    Serial.print(aebStageName(aebStage));

    if (echoDurationValue == 0) {
      Serial.print("  no echo, last ");
      Serial.print(aebLastValidCm, 1);
      Serial.println(" cm");
    } else {
      Serial.print("  at ");
      Serial.print(distanceCm, 1);
      Serial.println(" cm");
    }
  }
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

  // Deliberately behind no log switch. These print once at boot, and the
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

// Serves the page. PAGE_HTML is const and cannot be edited where it sits, so it
// is copied into a String, the interval is substituted in, and the copy is sent
// and released. That copy costs about 1.5 kB of heap for the length of one page
// load, not once per reading.
static void handleRoot() {
  String page = PAGE_HTML;

  // Every number the page needs comes from a constant here, so none of them is
  // written down twice. The slider's floor in particular will change once it
  // has been measured, and the page has to follow without being edited.
  page.replace("%POLL_MS%", String(WEB_POLL_INTERVAL_MS));
  page.replace("%SEND_MS%", String(COMMAND_SEND_INTERVAL_MS));
  page.replace("%MIN_PERCENT%", String(DRIVE_MIN_PERCENT));
  page.replace("%START_PERCENT%", String(DRIVE_START_PERCENT));

  server.send(200, "text/html", page);
}

// The readings, as JSON. Assembled by hand rather than with a library: four
// values do not justify the dependency, and this way the exact bytes going out
// are visible in the code.
//
// A missing echo is sent as null and never as a number. The browser has to be
// able to tell no reading from zero centimetres, because a zero would describe
// an obstacle pressed against the sensor - the same distinction the serial
// output makes when it prints "d=--  no echo", from the same source value.
//
// Neither handler measures anything. Both read what the loop last stored.
static void handleData() {
  String json = "{\"distance\":";

  if (echoDurationValue == 0) {
    json += "null";
  } else {
    json += String(distanceFromDuration(echoDurationValue), 1);
  }

  json += ",\"lineLeft\":";
  json += lineLeftValue;
  json += ",\"lineRight\":";
  json += lineRightValue;
  json += ",\"button\":";
  json += (lastButtonPressed ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

// Turns one command from the page into the two side values the motors take, and
// reports what it made of it. It does not drive: nothing here writes to the
// commanded values yet, so the channel can be proven end to end while the
// motors are still out of its reach.
//
// Everything is checked rather than trusted. The page cannot produce a speed
// outside the slider's range, so one arriving means something is wrong - a bug,
// a stale cached page, or a request made without the page at all, which anyone
// on this network can do. A request that is not fully understood is not a
// command, and it must not move a vehicle.
//
// Out of range is rejected rather than clamped. Clamping takes two rules where
// rejecting takes one, and it hides the case that matters: String::toInt()
// answers zero for "abc" exactly as it does for "0", so clamping would quietly
// turn nonsense into a crawl.
static void handleDrive() {
  const String dir = server.arg("dir");

  int16_t left = 0;
  int16_t right = 0;

  // Stop is answered first and without reading the speed at all. Stopping is
  // safe under every circumstance, and refusing one because some other field
  // was malformed would leave the rover driving on a validation error - the
  // exact outcome this handler exists to prevent.
  if (dir != "stop") {
    const String speedArg = server.arg("speed");
    const long percent = speedArg.toInt();

    // A missing argument needs no test of its own: server.arg() answers with an
    // empty string, toInt() reads that as zero, and zero is below the minimum.
    // The upper bound is written as itself because 100 is the top of a
    // percentage rather than a limit anyone could tune.
    //
    // Read into a long and range-checked before it is narrowed. Narrowing first
    // could fold an absurd value back inside the valid range and let it
    // through.
    if (percent < DRIVE_MIN_PERCENT || percent > 100) {
      Serial.print("DRIVE  rejected, speed=");
      Serial.println(speedArg);

      server.send(400, "text/plain", "bad speed");
      return;
    }

    // Casts rather than braces, the opposite of the constants above: this value
    // is not known at compile time, so braces could not check it. The range
    // test that just ran is the check.
    const uint8_t percentValue = static_cast<uint8_t>(percent);
    const int16_t magnitude =
        static_cast<int16_t>(dutyFromPercent(percentValue));

    // The one place in this project that knows what "left" means. Below it
    // there are two numbers and nothing else. There is no servo - a turn is a
    // difference between the two sides - and these turn on the spot, one side
    // driving forward against the other backward.
    if (dir == "fwd") {
      left = magnitude;
      right = magnitude;
    } else if (dir == "back") {
      left = -magnitude;
      right = -magnitude;
    } else if (dir == "left") {
      left = -magnitude;
      right = magnitude;
    } else if (dir == "right") {
      left = magnitude;
      right = -magnitude;
    } else {
      Serial.print("DRIVE  rejected, dir=");
      Serial.println(dir);

      server.send(400, "text/plain", "bad dir");
      return;
    }
  }

  // The command was understood: here is the new intent, and it is fresh as of
  // now. Both halves belong together, which is why they are written as one
  // block - an intent without a deadline would never expire.
  //
  // Only a command that got this far reaches these lines. A rejected request
  // proves that something is sending, not that an operator is there, and a
  // stream of nonsense must not keep the watchdog satisfied while the rover
  // carries on with the last order it did understand.
  //
  // These two assignments are the only reason this rover can move. Everything
  // else in this file writes zero to the motors or reads a sensor. left and
  // right are locals that would be gone the moment this function returned; the
  // copy is what makes the value outlive the request and reach the loop.
  //
  // Note what still does not happen here: nothing touches a motor. The loop
  // remains the only writer to the hardware, at a point of its own choosing,
  // which is where the AEB will intercept in phase 5.
  commandedLeft = left;
  commandedRight = right;

  lastCommandMs = millis();
  commandTimedOut = false;

  // Behind a switch because a held button produces about eight of these a
  // second, which is a repeating report. The two rejections above are not:
  // those are events, and they print whatever the switches say, like the
  // watchdog.
  if (LOG_DRIVE) {
    Serial.print("DRIVE  dir=");
    Serial.print(dir);
    Serial.print("  L=");
    Serial.print(left);
    Serial.print("  R=");
    Serial.println(right);
  }

  server.send(200, "text/plain", "ok");
}

// Registers the two routes and opens the socket on port 80. Placed after the
// handlers because it names them, and a function has to be declared before it
// can be referred to.
//
// on() does not run anything now. It fills in a routing table: when a GET for
// this path arrives, call that function. The method is given explicitly rather
// than left as HTTP_ANY, because both of these only read state. That
// distinction starts to matter in the next step, when a request that moves a
// motor must not be a GET.
//
// Nothing here reports failure, and saying so is more honest than an if that
// always passes. WebServer::begin() returns void. The socket underneath does
// track whether it is listening, but that flag is protected and the class
// exposes no accessor. The first request the browser makes is the real proof,
// and the address printed below is how to make one.
static void startWebServer() {
  server.on("/", HTTP_GET, handleRoot);
  server.on("/data", HTTP_GET, handleData);
  server.on("/drive", HTTP_POST, handleDrive);
  server.begin();

  Serial.print("WEB SERVER up  http://");
  Serial.print(WiFi.softAPIP());
  Serial.println("/");
}

void setup() {
  // First, before anything else: the buzzer sounds while its pin is undriven,
  // so every instruction that runs before this one is an instruction spent
  // making noise. Serial.begin() alone is long enough to hear.
  //
  // The order is forced by the core and cannot be inverted. A write to a pin
  // that has not been configured yet is dropped rather than queued: the
  // peripheral manager does not recognise the pin, gpio_set_level is never
  // reached, and the call only logs an error. Setting the latch ahead of
  // pinMode is an AVR habit, and here it silently did nothing - the pin stayed
  // at the sounding level through the whole of setup, and the boot log said so
  // on every reset.
  //
  // So the pin passes through its default low, which is the level that sounds,
  // for the microseconds between these two lines. That is as short as the
  // window gets.
  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, BUZZER_SILENT);

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
  //
  // Coast rather than brake: this is a rover that has not moved yet, so there
  // is no momentum to hold against, and braking would leave both enable pins
  // driven for the whole of setup() for nothing.
  drive(0, 0, StopMode::Coast);

  // TRIG is driven by us, ECHO is read. Parking TRIG LOW here means the module
  // is not looking at a half-raised line before the first measurement runs.
  pinMode(PIN_ULTRASONIC_TRIG, OUTPUT);
  digitalWrite(PIN_ULTRASONIC_TRIG, LOW);
  pinMode(PIN_ULTRASONIC_ECHO, INPUT);

  analogReadResolution(ADC_RESOLUTION_BITS);

  // Set as the default for channels not opened yet, rather than per pin. The
  // per-pin form reconfigures a channel that already exists, and the core only
  // opens an ADC channel on the first analogRead of that pin - so calling it
  // here, ahead of any read, found nothing to reconfigure and was dropped with
  // an error on every boot. This form sets the value the channels are built
  // with.
  //
  // It is also the core's own default, so the readings were never wrong and
  // the calibration figures above still stand. What was wrong was code that
  // claimed to set something it was not setting.
  analogSetAttenuation(ADC_11db);

  // The bus is opened here, with the pins named, rather than left to the
  // library. The last argument tells begin() not to call Wire.begin() itself:
  // it would re-open the bus on the core's default pins, which would only work
  // by coincidence.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  // begin() never asks the panel anything. It ignores every I2C ACK, and its
  // only failure path is the buffer allocation, so false means out of RAM and
  // not a missing display: an unplugged panel still returns true. Checked
  // anyway, because drawPixel indexes the buffer with no null check, so
  // drawing after a failed allocation writes through a null pointer.
  // Reported and then let go: the rover runs on without its screen.
  if (display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS, true, false)) {
    showTestScreen();
  } else {
    Serial.println("OLED buffer allocation FAILED - out of RAM");
  }

  startAccessPoint();
  startWebServer();

  // No initial write to either warning output. safeDrive sets both on every
  // pass, so the first one happens within a millisecond of the loop starting.
  Serial.print("SafeRover boot OK - AEB brake light on GPIO ");
  Serial.println(PIN_BRAKE_LIGHT);

  // A power-on self test, and a permanent one. It began as a way to prove the
  // buzzer worked at all, and the reason to keep it arrived later: its VCC is
  // unplugged by hand before every upload, because esptool holds the board in
  // ROM download mode where no firmware of ours runs to silence the pin.
  // Anything unplugged by hand can be left unplugged, and the AEB would then
  // warn in silence with nothing wrong in the code to find. This beep is what
  // says the wire went back. A module that dies quietly is a fault this project
  // has already had once, in the display.
  //
  // delay() is allowed here. The rule against it applies to the main loop,
  // where blocking means the sensors stop being read; setup() runs once before
  // that loop starts and holds nothing up.
  // Ungated, like the other lines printed at boot: it fires once and it is what
  // explains the noise that follows it.
  Serial.print("BUZZER TEST  ");
  Serial.print(BUZZER_TEST_MS);
  Serial.println(" ms beep on GPIO 4");

  digitalWrite(PIN_BUZZER, BUZZER_SOUND);
  delay(BUZZER_TEST_MS);
  digitalWrite(PIN_BUZZER, BUZZER_SILENT);
}

void loop() {
  const unsigned long now = millis();

  // Report transitions only, and only once the debounce window has passed. The
  // metal contacts bounce for a few milliseconds on every open and close, which
  // the loop is fast enough to read as a burst of separate edges.
  const bool buttonPressed = (digitalRead(PIN_MODE_BUTTON) == LOW);
  if (buttonPressed != lastButtonPressed &&
      now - lastButtonChange >= BUTTON_DEBOUNCE_MS) {
    lastButtonChange = now;
    lastButtonPressed = buttonPressed;

    if (LOG_BUTTON) {
      Serial.println(buttonPressed ? "BUTTON PRESSED" : "BUTTON RELEASED");
    }
  }

  // Data collection only: read and report. No thresholds, no decisions.
  if (now - lastLinePrint >= LINE_PRINT_INTERVAL_MS) {
    lastLinePrint = now;

    lineLeftValue = readLineAveraged(PIN_LINE_SENSOR_LEFT);
    lineRightValue = readLineAveraged(PIN_LINE_SENSOR_RIGHT);

    // A bench aid, not a control input. Both sensors read white while the rover
    // is inside the lane, so this difference rests at a non-zero constant there
    // and only moves once one of them reaches a stripe. How the LKA turns the
    // two readings into an error signal is still open.
    const int16_t delta = static_cast<int16_t>(lineLeftValue) -
                          static_cast<int16_t>(lineRightValue);

    if (LOG_LINE) {
      Serial.print("LINE  L=");
      Serial.print(lineLeftValue);
      Serial.print("  R=");
      Serial.print(lineRightValue);
      Serial.print("  delta=");
      Serial.println(delta);
    }
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
    echoDurationValue = readEchoMedian();

    if (LOG_DISTANCE) {
      Serial.print("DIST  echo=");
      Serial.print(echoDurationValue);
      Serial.print("us  ");

      // A timeout is not a distance. Converting the 0 would print 0.0 cm, which
      // reads exactly like an obstacle pressed against the sensor — the most
      // dangerous possible misreading once this drives the brakes.
      if (echoDurationValue == 0) {
        Serial.println("d=--  no echo");
      } else {
        Serial.print("d=");
        Serial.print(distanceFromDuration(echoDurationValue), 1);
        Serial.println("cm");
      }
    }

    // Inside the same block as the read, so the stage is only reconsidered
    // when there is something new to reconsider it from. Nothing acts on it
    // yet: safeDrive still forwards untouched.
    updateAebStage();
  }

  // The watchdog, and the single place the motors are written.
  //
  // Silence is read as a stop. The flag guards the condition rather than
  // letting it fire on every pass once the deadline is past: after the values
  // are zeroed there is nothing left to zero, and the message belongs to the
  // moment the link went quiet rather than to every pass afterwards. Behind no
  // log switch either, because those silence repeating reports and this is an
  // event - if it happened, it is exactly what you need to see.
  //
  // safeDrive runs every pass rather than on a timer. Writing the same duty
  // again is a few register writes that change nothing, while a timer would put
  // a delay between the moment something decides to stop and the moment the
  // motors are told. Placed after the distance read for the same reason: from
  // phase 5 the layer inside safeDrive wants the freshest measurement this pass
  // produced, not the previous one.
  if (!commandTimedOut && now - lastCommandMs >= COMMAND_TIMEOUT_MS) {
    commandedLeft = 0;
    commandedRight = 0;
    commandTimedOut = true;

    Serial.println("WATCHDOG  no command within timeout, motors stopped");
  }

  safeDrive(commandedLeft, commandedRight);

  // Answers whatever the browser has asked for, and it is the only moment the
  // server does anything at all. It has no task and no timer of its own; it
  // sits still until this line gives it a turn. That is why a synchronous
  // server was chosen over an asynchronous one - the scheduler stays in charge,
  // and no request handler can interrupt a distance measurement. From phase 5
  // that is not a detail, it is what makes the braking timing something that
  // can be reasoned about at all.
  //
  // Last in the loop on purpose, so it serves the values this pass just took
  // rather than the previous pass's.
  //
  // One thing this is NOT free of: with no request waiting, handleClient()
  // calls delay(1) before returning. The library assumes a loop that does
  // nothing else and needs somewhere to yield to FreeRTOS - and on a dual-core
  // ESP32 the Arduino loop task never yields on its own, so that assumption is
  // not unreasonable. server.enableDelay(false) removes it, and it is left on
  // deliberately. Switching it off makes yielding this loop's job, and the only
  // yield left would be the delay inside readEchoMedian, which is there to
  // space ultrasonic bursts rather than to feed the scheduler and which
  // disappears the day that measurement is made non-blocking. Turning it off
  // belongs in the same change as an explicit yield of our own, not in this
  // one.
  //
  // It also adds work to the loop. Nothing beside pulseIn today, which blocks
  // for up to 25 ms, but by phase 7 it stacks with that and with the sixteen
  // samples the line sensors take, and the three together are what the AEB
  // timing will have to survive.
  if (now - lastWebHandle >= WEB_HANDLE_INTERVAL_MS) {
    lastWebHandle = now;
    server.handleClient();
  }
}
