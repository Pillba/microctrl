#pragma once

#include <Arduino.h>

class StepperController
{
public:
    void begin(uint8_t stepPin, uint8_t directionPin, uint8_t resetPin);
    void stop();
    void moveSteps(long steps);

private:
    uint8_t stepPin_ = 0;
    uint8_t directionPin_ = 0;
    uint8_t resetPin_ = 0;
};
