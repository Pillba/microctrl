#include "servo.h"

void ServoController::begin(uint8_t pin)
{
    pin_ = pin;
    pinMode(pin_, OUTPUT);
    setAngle(0);
}

void ServoController::setAngle(uint8_t angle)
{
    angle_ = angle > 180 ? 180 : angle;
}

uint8_t ServoController::angle() const
{
    return angle_;
}
