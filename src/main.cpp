#include <Arduino.h>

// Phase 1 — non-blocking brake light.
// Pins follow the wiring contract in README.md. GPIO 16/17 from the original
// plan are not broken out on this 30-pin board, so the brake light is on 19.

// --- Pin assignments ---
constexpr uint8_t PIN_BRAKE_LIGHT = 19;

// --- Timing ---
constexpr unsigned long BRAKE_BLINK_INTERVAL_MS = 500;

// --- Serial ---
constexpr unsigned long SERIAL_BAUD = 115200;

// Store when the last toggle happened rather than when the next one is due:
// unsigned subtraction stays correct when millis() rolls over (~49 days).
unsigned long lastBrakeToggle = 0;
bool brakeLightOn = false;

void setup() {
  Serial.begin(SERIAL_BAUD);

  pinMode(PIN_BRAKE_LIGHT, OUTPUT);
  digitalWrite(PIN_BRAKE_LIGHT, LOW);

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
}
