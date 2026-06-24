#include <Arduino.h>

// 01_blink — בדיקת שרשרת הכלים. מהבהב את הלד הכחול המובנה.

void setup() {
  pinMode(2, OUTPUT);
  Serial.begin(115200);
  Serial.println("SafeRover boot OK");
}

void loop() {
  digitalWrite(2, HIGH);
  Serial.println("LED ON");
  delay(500);
  digitalWrite(2, LOW);
  Serial.println("LED OFF");
  delay(500);
}