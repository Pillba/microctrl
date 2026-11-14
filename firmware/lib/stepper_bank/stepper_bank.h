#pragma once

#include <board.h>

// Six DRV8805 stepper drivers sharing STEP and DIR. Only one is enabled at a time (active-low nENBL).
class StepperBank
{
public:
    // All drivers disabled, STEP low, DIR high, then a reset pulse.
    void begin();

    // Enables slot 1..6 and disables the others. Slot 0 disables all.
    void enable(uint8_t slot);
    void disableAll();

    // Enables the slot, steps, then disables every driver. If hook is set it runs every
    // board::STEPS_PER_POLL steps (starting at step 0); a true return stops early.
    // Returns the number of steps taken.
    uint32_t run(uint8_t slot, uint32_t steps, PollHook hook = nullptr);
};
