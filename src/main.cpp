#include <Arduino.h>

// Phase 1 — brake light, status LED and mode button.
// Pins follow the wiring contract in README.md. GPIO 16/17 from the original
// plan are not broken out on this 30-pin board, so the brake light is on 19
// and the status LED on 13.

// --- Pin assignments ---
constexpr uint8_t PIN_BRAKE_LIGHT = 19;
constexpr uint8_t PIN_STATUS_LED  = 13;
constexpr uint8_t PIN_MODE_BUTTON = 23;

// --- Timing ---
constexpr unsigned long BRAKE_BLINK_INTERVAL_MS = 500;

// --- Serial ---
constexpr unsigned long SERIAL_BAUD = 115200;

// Store when the last toggle happened rather than when the next one is due:
// unsigned subtraction stays correct when millis() rolls over (~49 days).
unsigned long lastBrakeToggle = 0;
bool brakeLightOn = false;

// Previous button reading, so the loop can report changes instead of flooding
// the serial line on every pass while the button is held down.
bool lastButtonPressed = false;

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(PIN_BRAKE_LIGHT, OUTPUT);
  digitalWrite(PIN_BRAKE_LIGHT, LOW);

  // Lit for as long as the board is powered, so it also shows at a glance
  // that the firmware actually reached setup().
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, HIGH);

  // INPUT_PULLUP switches on a resistor inside the ESP32 that holds the pin at
  // 3V3 while the button is open. The other leg of the button goes to GND, so
  // closing it drags the pin down and a press reads LOW. That internal resistor
  // is why no external one is wired.
  pinMode(PIN_MODE_BUTTON, INPUT_PULLUP);

  Serial.print("SafeRover boot OK - brake light on GPIO ");
  Serial.println(PIN_BRAKE_LIGHT);
}

void loop() {
  const unsigned long now = millis();

  // delay() is banned here by project convention: it blocks the whole loop,
  // and later phases must keep polling the ultrasonic sensor while this blinks.
  if (now - lastBrakeToggle >= BRAKE_BLINK_INTERVAL_MS) {
    lastBrakeToggle = now;

    brakeLightOn = !brakeLightOn;
    digitalWrite(PIN_BRAKE_LIGHT, brakeLightOn ? HIGH : LOW);

    Serial.println(brakeLightOn ? "BRAKE LIGHT ON" : "BRAKE LIGHT OFF");
  }

  // Report transitions only. Deliberately undebounced for now: the raw contact
  // bounce should be visible in the terminal before it gets filtered out.
  const bool buttonPressed = (digitalRead(PIN_MODE_BUTTON) == LOW);
  if (buttonPressed != lastButtonPressed) {
    lastButtonPressed = buttonPressed;

    Serial.println(buttonPressed ? "BUTTON PRESSED" : "BUTTON RELEASED");
  }
}
