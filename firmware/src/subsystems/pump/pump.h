#pragma once

#include <Arduino.h>

class PumpController
{
public:
    void begin(uint8_t pin);
    void setEnabled(bool enabled);
    bool enabled() const;

private:
    uint8_t pin_ = 0;
    bool enabled_ = false;
};
