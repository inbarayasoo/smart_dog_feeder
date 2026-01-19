#include <AccelStepper.h>

#define STEP_PIN 2
#define DIR_PIN  5

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

void setup() {
  stepper.setMaxSpeed(300);
  stepper.setAcceleration(100);


  stepper.setSpeed(-150); 
}

void loop() {
  stepper.runSpeed();
}
