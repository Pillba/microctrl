#pragma once

#include <board.h>
#include <dispense_rules.h>
#include <ir_beam.h>
#include <scale.h>
#include <shield.h>
#include <stepper_bank.h>

// One verified dispense:
//   1. close the shield (hall sensor confirms; a missing confirmation is only a warning)
//   2. tare the scale
//   3. run the stepper STEPS_PER_PILL * quantity
//   4. the IR beam must count at least quantity breaks
//   5. weight must arrive, settle, and match quantity * expected pill weight
//   6. open the shield
// On any failure the shield stays closed.

enum class DispenseEvent : uint8_t
{
    STARTED,
    SHIELD_CLOSING,
    SHIELD_CLOSED,     // report.shieldConfirmed
    TARING,
    TARED,
    STEPPING,
    STEPPED,
    IR_CHECKING,
    IR_CONFIRMED,      // report.trips
    WEIGHT_WAITING,
    WEIGHT_MEASURED,   // report.grams, report.steady
    WEIGHT_COMPARING,  // report.expectedG, report.toleranceG
    WEIGHT_MATCHED,
    SHIELD_OPENING,
    SHIELD_OPENED,     // report.shieldConfirmed
    PASSED,
    FAILED,            // report.code
};

struct DispenseReport
{
    uint8_t slot = 0;
    uint8_t quantity = 0;
    dispense::ResultCode code = dispense::RESULT_FAILED;
    uint8_t confirmedQuantity = 0;
    bool shieldConfirmed = false;
    unsigned long trips = 0;
    float grams = 0;
    bool steady = false;
    float expectedG = 0;
    float toleranceG = 0;
};

// Progress callback. Apps use it for text output; it must not start another dispense.
typedef void (*DispenseListener)(DispenseEvent event, const DispenseReport &report);

class DispenseSequence
{
public:
    DispenseSequence(StepperBank &steppers, Shield &shield, IrBeam &ir, Scale &scale)
        : steppers_(steppers), shield_(shield), ir_(ir), scale_(scale) {}

    void setListener(DispenseListener listener) { listener_ = listener; }

    // Runs while stepping and while waiting for IR and weight. Its return value is ignored:
    // a dispense always runs to completion once the stepper has started.
    void setPollHook(PollHook hook) { hook_ = hook; }

    void setPillWeight(float grams) { pillWeightG_ = grams; }
    float pillWeight() const { return pillWeightG_; }

    // Caller validates slot and quantity (dispense::validRequest).
    dispense::ResultCode run(uint8_t slot, uint8_t quantity, DispenseReport &report);

private:
    void emit(DispenseEvent event, const DispenseReport &report);
    dispense::ResultCode fail(dispense::ResultCode code, DispenseReport &report);
    void pollFor(unsigned long ms);

    StepperBank &steppers_;
    Shield &shield_;
    IrBeam &ir_;
    Scale &scale_;
    DispenseListener listener_ = nullptr;
    PollHook hook_ = nullptr;
    float pillWeightG_ = dispense::DEFAULT_PILL_WEIGHT_G;
};
