#include "stepper_bank.h"

namespace
{
const uint8_t ENABLE_PINS[board::STEPPER_COUNT] = {
    board::NENBL1_PIN, board::NENBL2_PIN, board::NENBL3_PIN,
    board::NENBL4_PIN, board::NENBL5_PIN, board::NENBL6_PIN,
};
}

void StepperBank::begin()
{
    disableAll();
    for (uint8_t i = 0; i < board::STEPPER_COUNT; i++) pinMode(ENABLE_PINS[i], OUTPUT);

    digitalWrite(board::STEP_PIN, LOW);
    pinMode(board::STEP_PIN, OUTPUT);
    digitalWrite(board::DIR_PIN, HIGH);
    pinMode(board::DIR_PIN, OUTPUT);

    digitalWrite(board::STEPPER_RESET_PIN, HIGH);
    pinMode(board::STEPPER_RESET_PIN, OUTPUT);
    delay(2);
    digitalWrite(board::STEPPER_RESET_PIN, LOW);
}

void StepperBank::disableAll()
{
    for (uint8_t i = 0; i < board::STEPPER_COUNT; i++) digitalWrite(ENABLE_PINS[i], HIGH);
}

void StepperBank::enable(uint8_t slot)
{
    disableAll();
    if (slot >= 1 && slot <= board::STEPPER_COUNT) digitalWrite(ENABLE_PINS[slot - 1], LOW);
}

uint32_t StepperBank::run(uint8_t slot, uint32_t steps, PollHook hook)
{
    enable(slot);
    delay(board::STEPPER_ENABLE_SETTLE_MS);

    uint32_t taken = 0;
    uint8_t untilPoll = 0;
    while (taken < steps)
    {
        digitalWrite(board::STEP_PIN, HIGH);
        delayMicroseconds(board::STEP_PULSE_US);
        digitalWrite(board::STEP_PIN, LOW);
        delayMicroseconds(board::STEP_PERIOD_US - board::STEP_PULSE_US);
        taken++;

        if (hook && untilPoll-- == 0)
        {
            untilPoll = board::STEPS_PER_POLL - 1;
            if (hook()) break;
        }
    }

    disableAll();
    return taken;
}
