#ifndef MOTOR_MANAGER_H
#define MOTOR_MANAGER_H

void initMotor();
void startMotor();
void stopMotor();
void updateMotor();
void startMotorBackward();
void startMotorRelativeSteps(long deltaSteps);
bool motorMoveDone();


extern bool is_motor_running;

#endif
