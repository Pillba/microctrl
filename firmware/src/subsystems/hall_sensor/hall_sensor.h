#pragma once

#include <Arduino.h>

class HallSensor
{
public:
    void begin(uint8_t pin);
    bool active() const;

private:
    uint8_t pin_ = 0;
};
