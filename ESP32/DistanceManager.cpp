#include "Adafruit_VL53L0X.h"
#include "DistanceManager.h"
#include <Wire.h>

// ---------- VL53L0X distance sensor configuration ----------
Adafruit_VL53L0X lox;
const uint16_t EMPTY_THRESHOLD_MM = 74;        
bool containerIsEmpty = false;
uint16_t distance_mm =0;

// Weight reading timing
unsigned long lastMeasureMillisDist = 0;
unsigned long now_distance = 0;
const unsigned long MEASURE_INTERVAL_MS = 200;  // Distance measurement interval (ms)

void initDistance(){ //setup for distance sensor

    // I2C for VL53L0X
    Wire.begin(21, 22);
    
    // Initialize distance sensor
    if (!lox.begin()) {
    // If distance sensor is not found, block here
        while (1) delay(100);
  }
}

void updateDistance(){ //measure distance
    now_distance = millis();
    if (now_distance - lastMeasureMillisDist < MEASURE_INTERVAL_MS) 
        return;
    lastMeasureMillisDist = now_distance;

    VL53L0X_RangingMeasurementData_t measure;
    lox.rangingTest(&measure, false);
    if (measure.RangeStatus != 4) {  // 4 = out of range
        distance_mm = measure.RangeMilliMeter;
        containerIsEmpty= (distance_mm > EMPTY_THRESHOLD_MM);
    }
}

bool isContainerEmpty() {
    // Returns the last stored result immediately without checking time
    return containerIsEmpty; 
}

