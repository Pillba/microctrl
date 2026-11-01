#pragma once

#include <Arduino.h>

class IrBreakBeam
{
public:
    void begin(uint8_t emitterPin, uint8_t receiverPin);
    bool broken() const;

private:
    uint8_t receiverPin_ = 0;
};
