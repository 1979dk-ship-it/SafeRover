#include <Arduino.h>

// Phase 1 — brake light on PWM, status LED and mode button.
// Phase 2 — reading the left line sensor. Data collection only: there are no
// thresholds here yet, because the values the sensor returns over white and over
// black have not been measured.
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

// --- ADC ---
// 12 bits gives a 0-4095 range, and ADC_11db widens the input range so the whole
// 0-3.3 V the sensor can put out is measurable.
constexpr uint8_t ADC_RESOLUTION_BITS = 12;

// The ADC is noisy: repeated reads of a steady voltage still differ by tens of
// counts. Averaging a burst of samples narrows that spread.
constexpr uint8_t LINE_SENSOR_SAMPLES = 16;

// --- PWM (LEDC) ---
// LEDC is a peripheral inside the ESP32, not a software loop. Once a channel is
// configured it keeps generating the waveform on its own, with no work from the
// CPU, which leaves the main loop free to read sensors.
constexpr uint8_t PWM_CHANNEL_BRAKE = 0;
// 5 kHz is far above the rate the eye can follow, so the light looks steady
// rather than flickering. 8 bits gives 256 steps, which is finer than anything
// needed to control motor speed.
constexpr uint32_t PWM_FREQUENCY_HZ = 5000;
constexpr uint8_t PWM_RESOLUTION_BITS = 8;
// Derived from the resolution so the two can never drift apart.
constexpr uint32_t PWM_MAX_DUTY =
    (1UL << PWM_RESOLUTION_BITS) - 1; // UL unsigend long

// --- Timing ---
constexpr unsigned long BRAKE_STEP_INTERVAL_MS = 1000;
constexpr unsigned long LINE_PRINT_INTERVAL_MS = 200;

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
}
