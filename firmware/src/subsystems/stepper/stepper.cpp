#include "stepper.h"

void StepperController::begin(uint8_t stepPin, uint8_t directionPin, uint8_t resetPin)
{
    stepPin_ = stepPin;
    directionPin_ = directionPin;
    resetPin_ = resetPin;
    pinMode(stepPin_, OUTPUT);
    pinMode(directionPin_, OUTPUT);
    pinMode(resetPin_, OUTPUT);
    digitalWrite(stepPin_, LOW);
    digitalWrite(directionPin_, LOW);
    digitalWrite(resetPin_, HIGH);
}

void StepperController::stop()
{
    digitalWrite(stepPin_, LOW);
}

void StepperController::moveSteps(long steps)
{
    digitalWrite(directionPin_, steps >= 0 ? HIGH : LOW);
}
