#pragma once

// Pure dispense rules (no Arduino dependency) so they can be unit tested on the host.

#include <stdint.h>

namespace dispense
{
constexpr uint8_t MIN_SLOT = 1;
constexpr uint8_t MAX_SLOT = 6;

constexpr float DEFAULT_PILL_WEIGHT_G = 0.95;
constexpr float WEIGHT_TOLERANCE_PCT = 15.0;     // allowed difference, percent of the expected weight
constexpr float WEIGHT_TOLERANCE_MIN_G = 0.030;  // but never less than this
constexpr float MIN_CHANGE_G = 0.030;            // less than this counts as "nothing reached the cup"

// Wire values of DISPENSE_RESULT.result_code. 0-3 match the codes the Pi already handles;
// 4 and up refine the old generic FAILED (1). See protocol/protocol.md.
enum ResultCode : uint8_t
{
    RESULT_OK = 0,
    RESULT_FAILED = 1,
    RESULT_INVALID_REQUEST = 2,
    RESULT_BUSY = 3,
    RESULT_SCALE_NOT_RESPONDING = 4,
    RESULT_IR_NOT_TRIPPED = 5,
    RESULT_NO_WEIGHT = 6,
    RESULT_WEIGHT_MISMATCH = 7,
};

inline bool validSlot(uint8_t slot)
{
    return slot >= MIN_SLOT && slot <= MAX_SLOT;
}

inline bool validRequest(uint8_t slot, uint8_t quantity)
{
    return validSlot(slot) && quantity > 0;
}

inline float absf(float value)
{
    return value < 0 ? -value : value;
}

inline float weightTolerance(float expectedG)
{
    float tolerance = expectedG * WEIGHT_TOLERANCE_PCT / 100.0f;
    return tolerance < WEIGHT_TOLERANCE_MIN_G ? WEIGHT_TOLERANCE_MIN_G : tolerance;
}

inline bool weightMatches(float measuredG, float expectedG)
{
    return absf(measuredG - expectedG) <= weightTolerance(expectedG);
}

inline bool weightArrived(float measuredG)
{
    return absf(measuredG) >= MIN_CHANGE_G;
}
} // namespace dispense
