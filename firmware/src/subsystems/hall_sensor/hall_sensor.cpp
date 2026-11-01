#include "hall_sensor.h"

void HallSensor::begin(uint8_t pin)
{
    pin_ = pin;
    pinMode(pin_, INPUT_PULLUP);
}

bool HallSensor::active() const
{
    return digitalRead(pin_) == LOW;
}
