#include "pump.h"

void PumpController::begin(uint8_t pin)
{
    pin_ = pin;
    pinMode(pin_, OUTPUT);
    setEnabled(false);
}

void PumpController::setEnabled(bool enabled)
{
    enabled_ = enabled;
    digitalWrite(pin_, enabled_ ? HIGH : LOW);
}

bool PumpController::enabled() const
{
    return enabled_;
}
