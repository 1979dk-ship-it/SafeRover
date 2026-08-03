#include <Arduino.h>
#include <Wire.h>

// A one-shot bench check, not part of the rover firmware. It is kept out of
// src/ and built as its own environment so it can be flashed on its own and
// never ends up compiled into the vehicle.
//
// What it is for: a blank OLED has three possible causes that all look
// identical from the outside — the wiring is wrong, the address is not the one
// assumed, or the controller chip is not the one the library expects. A display
// library cannot tell them apart, because it fails the same way in all three
// cases. This scanner separates them. If it reports an address, the wiring is
// good and the address is known, and only the controller chip is still open.
// If it reports nothing, the problem is below the software entirely and no
// library will fix it.
//
// Run this before loading any display library, not after.

constexpr uint8_t PIN_I2C_SDA = 21;
constexpr uint8_t PIN_I2C_SCL = 22;

// I2C reserves everything below 0x08 and above 0x77 for special functions such
// as the general call and 10-bit addressing, so no ordinary device answers
// outside this window. Scanning past it wastes time and can confuse devices.
constexpr uint8_t I2C_ADDRESS_FIRST = 0x08;
constexpr uint8_t I2C_ADDRESS_LAST = 0x77;

constexpr unsigned long SERIAL_BAUD = 115200;

void setup() {
  Serial.begin(SERIAL_BAUD);

  // The ESP32 can put I2C on almost any pin, so the pair has to be named
  // explicitly. These two match the wiring contract.
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

  Serial.println();
  Serial.print("I2C scan on SDA ");
  Serial.print(PIN_I2C_SDA);
  Serial.print(" / SCL ");
  Serial.println(PIN_I2C_SCL);

  uint8_t found = 0;

  for (uint8_t address = I2C_ADDRESS_FIRST; address <= I2C_ADDRESS_LAST;
       address++) {
    // Addressing a device and immediately ending the transmission sends the
    // address and nothing else. A device that recognises its own address pulls
    // the data line low to acknowledge, and endTransmission returns 0 for that
    // acknowledgement. Any other return code means nothing answered.
    Wire.beginTransmission(address);

    if (Wire.endTransmission() == 0) {
      found++;

      Serial.print("  device found at 0x");
      // HEX drops the leading zero, so 0x3C would print as 3C but 0x08 as 8.
      // Padding keeps every address two digits wide and comparable by eye.
      if (address < 0x10) {
        Serial.print('0');
      }
      Serial.println(address, HEX);
    }
  }

  Serial.println();

  // An empty result is still a result, and saying so explicitly is the point:
  // silence on the serial line is indistinguishable from a crashed sketch.
  if (found == 0) {
    Serial.println("no devices found");
    Serial.println("check SDA/SCL are not swapped, and that the device has "
                   "power and a shared ground");
  } else {
    Serial.print("scan complete - ");
    Serial.print(found);
    Serial.println(" device(s) found");
  }
}

void loop() {
  // Deliberately empty. The bus does not change while the board is powered, so
  // rescanning would only repeat the same answer and bury the first result in
  // scrollback. Press EN to run it again.
}
