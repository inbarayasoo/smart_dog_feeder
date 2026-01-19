#include <Wire.h>
#include "Adafruit_VL53L0X.h"

// ---------- VL53L0X Distance Sensor Settings ----------
Adafruit_VL53L0X lox;

const unsigned long MEASURE_INTERVAL_MS = 200;  // How often to measure
unsigned long lastMeasureMillis = 0;

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("ESP32 + VL53L0X simple distance test");

  // I2C (ESP32 default pins example: SDA=21, SCL=22)
  Wire.begin(21, 22);

  // VL53L0X
  if (!lox.begin()) {
    Serial.println("Failed to find VL53L0X sensor!");
    while (1) delay(100);
  }
  Serial.println("VL53L0X sensor OK!");

  lastMeasureMillis = millis();
}

// ---------- Distance update ----------
void updateDistance() {
  unsigned long now = millis();
  if (now - lastMeasureMillis < MEASURE_INTERVAL_MS) return;
  lastMeasureMillis = now;

  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);  // false = no debug prints

  if (measure.RangeStatus != 4) {    // 4 = out of range
    Serial.print("Distance: ");
    Serial.print(measure.RangeMilliMeter);
    Serial.println(" mm");
  } else {
    Serial.println("Out of range");
  }
}

// ---------- loop ----------
void loop() {
  updateDistance();
}
