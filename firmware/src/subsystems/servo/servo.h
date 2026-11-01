#pragma once

#include <Arduino.h>

class ServoController
{
public:
    void begin(uint8_t pin);
    void setAngle(uint8_t angle);
    uint8_t angle() const;

private:
    uint8_t pin_ = 0;
    uint8_t angle_ = 0;
};
