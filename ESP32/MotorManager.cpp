#include "MotorManager.h"
#include <AccelStepper.h>
#include <math.h>   // fabs()

// ---------- Stepper motor configuration ----------
#define STEP_PIN 2
#define DIR_PIN  5

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);


const float MOTOR_SPEED_STEPS_PER_SEC = -350.0;   
const long  CONTINUOUS_TARGET        = -2000000000L;

volatile bool stop_motor = false;
bool is_motor_running = false;

void initMotor() {
  // Conservative settings to reduce stalls
  stepper.setMaxSpeed(450);      
  stepper.setAcceleration(200);  
  stepper.setCurrentPosition(0);
}

// Helper: start continuous run toward a given absolute target,
// but reset currentPosition to avoid 32-bit overflow when flipping direction.
static void startMotorToTarget(long target) {
  stop_motor = false;
  is_motor_running = true;

  stepper.setMaxSpeed(fabs(MOTOR_SPEED_STEPS_PER_SEC));

  
  stepper.setCurrentPosition(0);

  stepper.moveTo(target);
}

// Normal feeding direction (exactly as you had it)
void startMotor() {
  const long normalTarget =
      (MOTOR_SPEED_STEPS_PER_SEC < 0) ? CONTINUOUS_TARGET : -CONTINUOUS_TARGET;

  startMotorToTarget(normalTarget);
}

// Reverse direction (opposite of startMotor)
void startMotorBackward() {
  const long normalTarget =
      (MOTOR_SPEED_STEPS_PER_SEC < 0) ? CONTINUOUS_TARGET : -CONTINUOUS_TARGET;

  const long reverseTarget = -normalTarget; // flip direction safely
  startMotorToTarget(reverseTarget);
}

void stopMotor() { //stops the motor
  stop_motor = true;
  stepper.stop(); // smooth deceleration using acceleration
}

void updateMotor() { //change motor speed
  // MUST be called very frequently for smooth stepping
  stepper.run();

  if (stop_motor && stepper.distanceToGo() == 0) {
    is_motor_running = false;
  }
  //stepper.runSpeed(); // generates steps based on setSpeed()
}


static const long STEPS_PER_REV_EFFECTIVE = 3200;

void startMotorRelativeSteps(long deltaSteps) { //helper for revesing the motor
  stop_motor = false;
  is_motor_running = true;

  stepper.setMaxSpeed(450);
  stepper.setAcceleration(800); // faster ramp for wiggle

  stepper.setCurrentPosition(0);
  stepper.moveTo(deltaSteps);
}

bool motorMoveDone() { //make sure the motor has stopped
  return stepper.distanceToGo() == 0;
}
