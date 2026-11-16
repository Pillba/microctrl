#pragma once

#include <board.h>
#include <dispense_rules.h>
#include <ir_beam.h>
#include <pump.h>
#include <scale.h>
#include <shield.h>
#include <stepper_bank.h>

// One verified dispense.
//
// Pills (slot 1-6):
//   1. close the shield (hall sensor confirms if fitted; a missing confirmation is only a warning)
//   2. tare the scale
//   3. run the stepper until the IR beam has counted quantity breaks, at most
//      MAX_STEP_TURNS * STEPS_PER_PILL * quantity steps
//   4. the IR beam must count at least quantity breaks (a pill still falling gets IR_WAIT_MS)
//   5. weight must arrive, settle, and match quantity * expected pill weight
//   6. open the shield
//
// Liquid (slot 7, quantity in mL):
//   1. close the shield
//   2. tare the scale
//   3. run the pump until the cup gains mL * g/mL (no flow within LIQUID_PRIME_MS: NO_WEIGHT;
//      at most pumpBudgetMs(mL); the pump is off on every exit)
//   4. let the tube drip, then the weight must settle and match within the liquid tolerance
//   5. open the shield
//
// On any failure the shield stays closed.

enum class DispenseEvent : uint8_t
{
    STARTED,
    SHIELD_CLOSING,
    SHIELD_CLOSED,     // report.shieldConfirmed
    TARING,
    TARED,
    STEPPING,          // report.steps = the step limit
    STEPPED,           // report.steps = steps taken
    IR_CHECKING,
    IR_CONFIRMED,      // report.trips
    PUMPING,           // report.expectedG (target)
    PUMPED,            // report.pumpMs, report.grams (reading when the pump stopped)
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
    uint8_t quantity = 0;              // pills, or mL for the liquid slot
    dispense::ResultCode code = dispense::RESULT_FAILED;
    uint8_t confirmedQuantity = 0;
    bool shieldConfirmed = false;
    unsigned long trips = 0;
    uint32_t steps = 0;
    float grams = 0;
    bool steady = false;
    float unitG = 0;                   // weight of one pill / one mL used for this dispense
    float expectedG = 0;
    float toleranceG = 0;
    unsigned long pumpMs = 0;
};

// Progress callback. Apps use it for text output; it must not start another dispense.
typedef void (*DispenseListener)(DispenseEvent event, const DispenseReport &report);

class DispenseSequence
{
public:
    DispenseSequence(StepperBank &steppers, Shield &shield, IrBeam &ir, Scale &scale, Pump &pump)
        : steppers_(steppers), shield_(shield), ir_(ir), scale_(scale), pump_(pump) {}

    void setListener(DispenseListener listener) { listener_ = listener; }

    // Runs while the shield moves, while stepping or pumping, and while waiting for IR and weight.
    // Its return value is ignored: a dispense always runs to completion once it has started.
    void setPollHook(PollHook hook) { hook_ = hook; }

    void setPillWeight(float grams) { pillWeightG_ = grams; }
    float pillWeight() const { return pillWeightG_; }

    // Caller validates slot and quantity (dispense::validRequest). Slot 7 is the liquid dispenser.
    // unitG is the weight of one pill (or of one mL for slot 7) for this dispense; 0 or less uses
    // pillWeight() (or DEFAULT_LIQUID_G_PER_ML).
    dispense::ResultCode run(uint8_t slot, uint8_t quantity, DispenseReport &report, float unitG = 0);

private:
    dispense::ResultCode runPills(uint8_t slot, uint8_t quantity, DispenseReport &report);
    dispense::ResultCode runLiquid(uint8_t ml, DispenseReport &report);
    // Pumps until the target weight; turns the pump off before returning. RESULT_OK when it stopped
    // at the target or at the time budget (the weighing decides), otherwise why it stopped early.
    dispense::ResultCode pumpToWeight(uint8_t ml, DispenseReport &report);
    void closeShieldAndTare(DispenseReport &report);
    dispense::ResultCode openShieldAndPass(DispenseReport &report);
    void emit(DispenseEvent event, const DispenseReport &report);
    dispense::ResultCode fail(dispense::ResultCode code, DispenseReport &report);
    void pollFor(unsigned long ms);

    StepperBank &steppers_;
    Shield &shield_;
    IrBeam &ir_;
    Scale &scale_;
    Pump &pump_;
    DispenseListener listener_ = nullptr;
    PollHook hook_ = nullptr;
    float pillWeightG_ = dispense::DEFAULT_PILL_WEIGHT_G;
};
