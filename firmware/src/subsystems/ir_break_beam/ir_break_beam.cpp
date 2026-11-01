#include "ir_break_beam.h"

void IrBreakBeam::begin(uint8_t emitterPin, uint8_t receiverPin)
{
    receiverPin_ = receiverPin;
    pinMode(emitterPin, OUTPUT);
    digitalWrite(emitterPin, HIGH);
    pinMode(receiverPin_, INPUT_PULLUP);
}

bool IrBreakBeam::broken() const
{
    return digitalRead(receiverPin_) == HIGH;
}
