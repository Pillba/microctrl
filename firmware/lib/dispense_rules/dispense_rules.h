#pragma once

// Pure dispense rules (no Arduino dependency) so they can be unit tested on the host.

#include <stdint.h>

namespace dispense
{
// Pill slots: one stepper each.
constexpr uint8_t MIN_SLOT = 1;
constexpr uint8_t MAX_SLOT = 6;

constexpr float DEFAULT_PILL_WEIGHT_G = 0.95;
// 45 %: a 0.40 g target accepts 0.22 to 0.58 g. With 3 or more pills one pill is less than 45 % of
// the total, so the weight alone no longer catches a missing or extra pill; the IR count still does.
constexpr float WEIGHT_TOLERANCE_PCT = 45.0;     // allowed difference, percent of the expected weight
constexpr float WEIGHT_TOLERANCE_MIN_G = 0.030;  // but never less than this
constexpr float WEIGHT_COMPARE_SLACK_G = 0.0005; // float rounding: a reading exactly on the limit passes
constexpr float MIN_CHANGE_G = 0.030;            // less than this counts as "nothing reached the cup"

// Liquid slot: the pump fills the same weighed cup. The DISPENSE_CMD quantity is whole mL.
// Tune the tolerance and timing on the bench (firmware/README.md, "Liquid").
constexpr uint8_t LIQUID_SLOT = 7;
constexpr float DEFAULT_LIQUID_G_PER_ML = 1.00;  // water; used when a DISPENSE_CMD carries no unit weight
constexpr float LIQUID_TOLERANCE_PCT = 10.0;     // allowed difference, percent of the expected weight
constexpr float LIQUID_TOLERANCE_MIN_G = 0.5;    // but never less than this (pump overshoot and drips)
constexpr uint8_t LIQUID_MAX_ML = 30;            // the cup's safe volume; larger requests are INVALID_REQUEST
// Pump time budget: LIQUID_PRIME_MS + LIQUID_MS_PER_ML * mL. Within LIQUID_PRIME_MS some liquid must
// reach the cup, otherwise the pump stops with NO_WEIGHT. The Pi's response timeout mirrors these
// (protocol/protocol.md): change both together.
constexpr uint32_t LIQUID_PRIME_MS = 5000;
constexpr uint32_t LIQUID_MS_PER_ML = 2000;      // assumes the pump delivers at least 0.5 mL/s

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

// A pill slot (one stepper).
inline bool validSlot(uint8_t slot)
{
    return slot >= MIN_SLOT && slot <= MAX_SLOT;
}

inline bool isLiquidSlot(uint8_t slot)
{
    return slot == LIQUID_SLOT;
}

// Pills: slot 1-6, at least one pill. Liquid: slot 7, 1 to LIQUID_MAX_ML mL.
inline bool validRequest(uint8_t slot, uint8_t quantity)
{
    if (quantity == 0) return false;
    if (validSlot(slot)) return true;
    return isLiquidSlot(slot) && quantity <= LIQUID_MAX_ML;
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
    return absf(measuredG - expectedG) <= weightTolerance(expectedG) + WEIGHT_COMPARE_SLACK_G;
}

inline bool weightArrived(float measuredG)
{
    return absf(measuredG) >= MIN_CHANGE_G;
}

inline float liquidTargetG(uint8_t ml, float gramsPerMl = DEFAULT_LIQUID_G_PER_ML)
{
    return ml * gramsPerMl;
}

inline float liquidTolerance(float expectedG)
{
    float tolerance = expectedG * LIQUID_TOLERANCE_PCT / 100.0f;
    return tolerance < LIQUID_TOLERANCE_MIN_G ? LIQUID_TOLERANCE_MIN_G : tolerance;
}

inline bool liquidMatches(float measuredG, float expectedG)
{
    return absf(measuredG - expectedG) <= liquidTolerance(expectedG);
}

// Longest the pump may run for this many mL.
inline uint32_t pumpBudgetMs(uint8_t ml)
{
    return LIQUID_PRIME_MS + LIQUID_MS_PER_ML * ml;
}
} // namespace dispense
