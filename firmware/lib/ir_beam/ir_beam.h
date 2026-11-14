#pragma once

#include <board.h>

// IR break beam on a pin-change interrupt. Counts beam breaks at least IR_MIN_GAP_US apart.
// Only one instance may exist: the ISRs in ir_beam.cpp write to shared counters.
class IrBeam
{
public:
    // Plain input, learns the clear level (keep the beam CLEAR during power up), enables PCINT.
    void begin();

    unsigned long breaks() const;   // atomic read of the break counter
    bool clearReadsHigh() const { return trippedLevel_ == LOW; }

private:
    uint8_t trippedLevel_ = LOW;
};
