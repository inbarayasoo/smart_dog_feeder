#include <HX711.h>
#include <algorithm>
#include "ScaleManager.h"


// ---------- HX711 load cell configuration ----------
#define LOADCELL_DOUT_PIN  16   // HX711 DT
#define LOADCELL_SCK_PIN   4    // HX711 SCK

HX711 scale;
const float CALIBRATION_FACTOR = 989.1836735f;  // Your calibration factor
bool scaleReady = false;



// Weight variables
float currentWeightGrams      = 0.0f;


// Weight reading timing
unsigned long lastWeightReadMillis = 0;
unsigned long now = 0;
const unsigned long WEIGHT_READ_INTERVAL_MS = 100;  // How often to refresh weight (ms)


// ---------- Initialize load cell (HX711) ----------
void initScale() {
    lastWeightReadMillis = millis();
    scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);
    delay(10000);
    if (scale.is_ready()) {

        scale.set_scale(CALIBRATION_FACTOR);  // Apply calibration factor
        scale.tare(20);// Tare with empty bowl (average of ~20 samples)
        currentWeightGrams = 0.0f;                      
        scaleReady = true;
    } 
    else {
    scaleReady = false;

    }
}


// ---------- Update weight (non-blocking style) ----------
void updateWeight() {// measure weight on the plate
  if (!scaleReady){

    return;
  }

  now = millis();
  if (now - lastWeightReadMillis < WEIGHT_READ_INTERVAL_MS) return;
  lastWeightReadMillis = now;

  if (scale.is_ready()) {
    // Single sample + simple low-pass filter to smooth out vibrations/noise
    float raw = scale.get_units(5);  // 1 sample
    currentWeightGrams = 0.7f * currentWeightGrams + 0.3f * raw;
    //currentWeightGrams = raw;
    //Serial.println(scale.is_ready() ? "HX711 READY" : "HX711 NOT READY");

    
  }
}

float getWeight(){

  return currentWeightGrams;
}

float getWeight2() { //depracated function
  int weights[11];
  for(int i = 0; i < 11; i++){
    updateWeight();
    weights[i] = currentWeightGrams;
    delay(100);
  }
  std::sort(weights, weights + 11);

  return weights[6];

}

void reZeroScale() { //reset the scales to minimize weight error
  if (!scaleReady) return;
  if (!scale.is_ready()) return;

  scale.tare(20);          // set new offset at current load
  currentWeightGrams = 0.0f;    // IMPORTANT: reset your low-pass filter state
}
