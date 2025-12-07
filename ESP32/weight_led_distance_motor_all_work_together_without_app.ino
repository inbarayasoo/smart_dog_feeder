#include <Wire.h>
#include "Adafruit_VL53L0X.h"
#include <Adafruit_NeoPixel.h>
#include <AccelStepper.h>
#include <HX711.h>

// ---------- Stepper motor configuration ----------
#define STEP_PIN 2
#define DIR_PIN  5

// Step/Dir driver
AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

// Negative direction = direction that dispenses food (based on your tests)
const float MOTOR_SPEED_STEPS_PER_SEC = -300.0;  // You can tune this (e.g., -200 / -400)

// ---------- VL53L0X distance sensor configuration ----------
Adafruit_VL53L0X lox;
const unsigned long MEASURE_INTERVAL_MS = 200;  // Distance measurement interval (ms)
const uint16_t EMPTY_THRESHOLD_MM = 140;        // 14 cm = 140 mm (container considered empty above this)

unsigned long lastMeasureMillis = 0;
bool containerEmpty = false;     // true when the food container is empty

// ---------- NeoPixel configuration ----------
#define NEOPIXEL_PIN 12
#define NUM_PIXELS   5

Adafruit_NeoPixel pixels(NUM_PIXELS, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);

const unsigned long BLINK_INTERVAL_MS = 300;
unsigned long lastBlinkMillis = 0;
bool ledOn = false;

// ---------- Feed button ----------
#define FEED_BUTTON_PIN 15   // Button between GPIO 15 and GND (using INPUT_PULLUP)

// ---------- Motor state based on container ----------
enum MotorState {
  MOTOR_ENABLED,   // Allowed to rotate (if feeding is active)
  MOTOR_DISABLED   // Container empty -> motor completely disabled
};

MotorState motorState = MOTOR_ENABLED;

// ---------- HX711 load cell configuration ----------
#define LOADCELL_DOUT_PIN  16   // HX711 DT
#define LOADCELL_SCK_PIN   4    // HX711 SCK

HX711 scale;
const float CALIBRATION_FACTOR = 1038.5f;  // Your calibration factor
bool scaleReady = false;

// How many grams to add on each button press (relative to current weight)
const float FEED_PORTION_GRAMS = 50.0f;

// ---------- Feeding state machine ----------
enum FeedState {
  FEED_IDLE,    // No feeding in progress, waiting for button press
  FEED_ACTIVE   // Feeding in progress – motor running until target weight is reached
};

FeedState feedState = FEED_IDLE;

// Weight variables
float currentWeightGrams      = 0.0f;
float feedTargetWeightGrams   = 0.0f;   // Target = current weight + FEED_PORTION_GRAMS for each press

// Weight reading timing
unsigned long lastWeightReadMillis = 0;
const unsigned long WEIGHT_READ_INTERVAL_MS = 100;  // How often to refresh weight (ms)

// To avoid stopping due to a single noisy spike
int aboveTargetCount = 0;
const int REQUIRED_ABOVE_TARGET = 3;   // Require N consecutive readings above target

// Safety timeout for feeding
const unsigned long FEED_TIMEOUT_MS = 8000;
unsigned long feedStartMillis = 0;

// For edge detection on button (short press)
bool prevButtonPressed = false;

// ---------- NeoPixel helpers ----------
void neoOff() {
  pixels.clear();
  pixels.show();
}

void neoSetColorAll(uint8_t r, uint8_t g, uint8_t b) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, pixels.Color(r, g, b));
  }
  pixels.show();
}

// ---------- Initialize load cell (HX711) ----------
void initScale() {
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  if (scale.is_ready()) {
    scale.set_scale(CALIBRATION_FACTOR);  // Apply calibration factor
    scale.tare(20);                       // Tare with empty bowl (average of ~20 samples)
    scaleReady = true;
  } else {
    scaleReady = false;
  }
}

// ---------- Update weight (non-blocking style) ----------
void updateWeight() {
  if (!scaleReady) return;

  unsigned long now = millis();
  if (now - lastWeightReadMillis < WEIGHT_READ_INTERVAL_MS) return;
  lastWeightReadMillis = now;

  if (scale.is_ready()) {
    // Single sample + simple low-pass filter to smooth out vibrations/noise
    float raw = scale.get_units(1);  // 1 sample
    currentWeightGrams = 0.7f * currentWeightGrams + 0.3f * raw;
  }
}

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  delay(1000);

  // I2C for VL53L0X
  Wire.begin(21, 22);

  // Initialize distance sensor
  if (!lox.begin()) {
    // If distance sensor is not found, block here
    while (1) delay(100);
  }

  // Initialize NeoPixel strip
  pixels.begin();
  neoOff();

  // Stepper motor configuration
  stepper.setMaxSpeed(600);
  stepper.setAcceleration(300);
  stepper.setSpeed(0);

  // Feed button (active LOW due to INPUT_PULLUP)
  pinMode(FEED_BUTTON_PIN, INPUT_PULLUP);

  // Initialize load cell
  initScale();

  // Timers
  lastMeasureMillis    = millis();
  lastBlinkMillis      = millis();
  lastWeightReadMillis = millis();
}

// ---------- Update distance and container-empty status ----------
void updateDistanceAndContainerStatus() {
  unsigned long now = millis();
  if (now - lastMeasureMillis < MEASURE_INTERVAL_MS) return;
  lastMeasureMillis = now;

  VL53L0X_RangingMeasurementData_t measure;
  lox.rangingTest(&measure, false);

  if (measure.RangeStatus != 4) {  // 4 = out of range
    uint16_t distance_mm = measure.RangeMilliMeter;

    bool prevEmpty = containerEmpty;
    containerEmpty = (distance_mm > EMPTY_THRESHOLD_MM);

    if (!prevEmpty && containerEmpty) {
      // Container just became empty → disable motor entirely
      motorState = MOTOR_DISABLED;
      stepper.setSpeed(0);
      feedState = FEED_IDLE;
    } else if (prevEmpty && !containerEmpty) {
      // Container was refilled → allow feeding again
      motorState = MOTOR_ENABLED;
    }
  }
}

// ---------- Update NeoPixel (blink red when container is empty) ----------
void updateNeoPixel() {
  unsigned long now = millis();

  if (containerEmpty) {
    // Blink all NeoPixels when container is empty
    if (now - lastBlinkMillis >= BLINK_INTERVAL_MS) {
      lastBlinkMillis = now;
      ledOn = !ledOn;
      if (ledOn) {
        neoSetColorAll(255, 0, 0);  // Red
      } else {
        neoOff();
      }
    }
  } else {
    // When container is not empty, ensure LEDs are off
    if (ledOn) {
      ledOn = false;
      neoOff();
    }
  }
}

// ---------- Motor + feeding logic (state machine) ----------
void updateMotorAndFeeding() {
  // Button is active LOW due to INPUT_PULLUP
  bool buttonPressed    = (digitalRead(FEED_BUTTON_PIN) == LOW);
  bool buttonRisingEdge = (buttonPressed && !prevButtonPressed);
  prevButtonPressed = buttonPressed;

  // If container is empty, feeding is not allowed
  if (motorState == MOTOR_DISABLED || containerEmpty) {
    stepper.setSpeed(0);
    feedState = FEED_IDLE;
    aboveTargetCount = 0;
    stepper.runSpeed();
    return;
  }

  switch (feedState) {
    case FEED_IDLE:
      // Motor is stopped when idle
      stepper.setSpeed(0);
      aboveTargetCount = 0;

      // Start a new feeding cycle on a short button press
      if (buttonRisingEdge) {
        // Relative logic: for each press, set a new target
        // target = current weight + FEED_PORTION_GRAMS
        feedTargetWeightGrams = currentWeightGrams + FEED_PORTION_GRAMS;
        feedStartMillis       = millis();
        feedState             = FEED_ACTIVE;
        stepper.setSpeed(MOTOR_SPEED_STEPS_PER_SEC);
      }
      break;

    case FEED_ACTIVE:
      // Safety: if container becomes empty during feeding, stop immediately
      if (containerEmpty) {
        stepper.setSpeed(0);
        feedState = FEED_IDLE;
        aboveTargetCount = 0;
        break;
      }

      // Check if we have reached (or passed) the target weight
      if (currentWeightGrams >= feedTargetWeightGrams) {
        aboveTargetCount++;
      } else if (aboveTargetCount > 0) {
        // Small hysteresis to avoid bouncing around the threshold
        aboveTargetCount--;
      }

      // Stop after N consecutive readings above target
      if (aboveTargetCount >= REQUIRED_ABOVE_TARGET) {
        stepper.setSpeed(0);
        feedState = FEED_IDLE;
      }
      // Safety timeout in case sensor or mechanics misbehave
      else if (millis() - feedStartMillis > FEED_TIMEOUT_MS) {
        stepper.setSpeed(0);
        feedState = FEED_IDLE;
        aboveTargetCount = 0;
      }
      // Otherwise keep rotating to dispense more food
      else {
        stepper.setSpeed(MOTOR_SPEED_STEPS_PER_SEC);
      }
      break;
  }

  // Must be called as often as possible so the stepper can generate smooth motion
  stepper.runSpeed();
}

// ---------- main loop ----------
void loop() {
  updateDistanceAndContainerStatus();  // Check if container is empty or refilled
  updateNeoPixel();                    // Handle LED indication
  updateWeight();                      // Refresh weight from load cell
  updateMotorAndFeeding();             // Handle feeding logic + motor control
}
