#include "dispense_sequence.h"

namespace
{
// Stop condition for StepperBank::run: the beam has counted every requested pill.
struct BreakTarget
{
    const IrBeam *ir;
    unsigned long initialBreaks;
    uint8_t quantity;
};

bool beamCountReached(void *context)
{
    const BreakTarget &target = *static_cast<const BreakTarget *>(context);
    return target.ir->breaks() - target.initialBreaks >= target.quantity;
}
}  // namespace

void DispenseSequence::emit(DispenseEvent event, const DispenseReport &report)
{
    if (listener_) listener_(event, report);
}

dispense::ResultCode DispenseSequence::fail(dispense::ResultCode code, DispenseReport &report)
{
    report.code = code;
    report.confirmedQuantity = 0;
    emit(DispenseEvent::FAILED, report);
    return code;
}

void DispenseSequence::pollFor(unsigned long ms)
{
    if (!hook_)
    {
        delay(ms);
        return;
    }
    const unsigned long start = millis();
    while (millis() - start < ms)
    {
        hook_();
        delay(10);
    }
}

dispense::ResultCode DispenseSequence::run(uint8_t slot, uint8_t quantity, DispenseReport &report, float unitG)
{
    report = DispenseReport();
    report.slot = slot;
    report.quantity = quantity;
    if (unitG > 0) report.unitG = unitG;
    else report.unitG = dispense::isLiquidSlot(slot) ? dispense::DEFAULT_LIQUID_G_PER_ML : pillWeightG_;
    emit(DispenseEvent::STARTED, report);

    if (!scale_.ready()) return fail(dispense::RESULT_SCALE_NOT_RESPONDING, report);

    if (dispense::isLiquidSlot(slot)) return runLiquid(quantity, report);
    return runPills(slot, quantity, report);
}

// 1. shield closed (confirmed by the hall sensor when fitted)  2. tare, so only this dispense is weighed
void DispenseSequence::closeShieldAndTare(DispenseReport &report)
{
    emit(DispenseEvent::SHIELD_CLOSING, report);
    report.shieldConfirmed = shield_.move(false, board::SHIELD_WAIT_MS, hook_);
    emit(DispenseEvent::SHIELD_CLOSED, report);

    emit(DispenseEvent::TARING, report);
    scale_.tare(board::AVERAGE_SCANS);
    emit(DispenseEvent::TARED, report);
}

// Everything checked: open the shield.
dispense::ResultCode DispenseSequence::openShieldAndPass(DispenseReport &report)
{
    emit(DispenseEvent::SHIELD_OPENING, report);
    report.shieldConfirmed = shield_.move(true, board::SHIELD_WAIT_MS, hook_);
    emit(DispenseEvent::SHIELD_OPENED, report);

    report.code = dispense::RESULT_OK;
    report.confirmedQuantity = report.quantity;
    emit(DispenseEvent::PASSED, report);
    return dispense::RESULT_OK;
}

dispense::ResultCode DispenseSequence::runPills(uint8_t slot, uint8_t quantity, DispenseReport &report)
{
    closeShieldAndTare(report);

    // 3. stepper: until the beam has counted every pill, or the step limit
    BreakTarget target{&ir_, ir_.breaks(), quantity};
    const unsigned long initialBreaks = target.initialBreaks;
    report.steps = (uint32_t)board::STEPS_PER_PILL * board::MAX_STEP_TURNS * quantity;
    emit(DispenseEvent::STEPPING, report);
    report.steps = steppers_.run(slot, report.steps, hook_, beamCountReached, &target);
    emit(DispenseEvent::STEPPED, report);

    // 4. IR break beam
    emit(DispenseEvent::IR_CHECKING, report);
    const unsigned long waitStart = millis();
    while (millis() - waitStart < board::IR_WAIT_MS && ir_.breaks() - initialBreaks < quantity)
    {
        if (hook_) hook_();
    }
    pollFor(board::IR_SETTLE_MS);
    report.trips = ir_.breaks() - initialBreaks;
    if (report.trips < quantity) return fail(dispense::RESULT_IR_NOT_TRIPPED, report);
    emit(DispenseEvent::IR_CONFIRMED, report);

    // 5. load cell: weight must arrive, settle, then match
    emit(DispenseEvent::WEIGHT_WAITING, report);
    if (!scale_.waitForWeight(board::WEIGHT_WAIT_MS, hook_))
        return fail(dispense::RESULT_NO_WEIGHT, report);
    report.steady = scale_.steadyGrams(report.grams, hook_);
    emit(DispenseEvent::WEIGHT_MEASURED, report);

    report.expectedG = report.unitG * quantity;
    report.toleranceG = dispense::weightTolerance(report.expectedG);
    emit(DispenseEvent::WEIGHT_COMPARING, report);
    if (!dispense::weightMatches(report.grams, report.expectedG))
        return fail(dispense::RESULT_WEIGHT_MISMATCH, report);
    emit(DispenseEvent::WEIGHT_MATCHED, report);

    // 6. open the shield
    return openShieldAndPass(report);
}

dispense::ResultCode DispenseSequence::pumpToWeight(uint8_t ml, DispenseReport &report)
{
    const uint32_t budgetMs = dispense::pumpBudgetMs(ml);
    dispense::ResultCode outcome = dispense::RESULT_OK;
    float grams = 0;
    bool flowing = false;

    const unsigned long start = millis();
    pump_.set(true);
    while (true)
    {
        if (hook_) hook_();
        const unsigned long elapsed = millis() - start;
        if (!scale_.tryGrams(grams))
        {
            outcome = dispense::RESULT_SCALE_NOT_RESPONDING;
            break;
        }
        if (dispense::weightArrived(grams)) flowing = true;
        if (grams >= report.expectedG) break;  // target reached
        if (!flowing && elapsed >= dispense::LIQUID_PRIME_MS)
        {
            outcome = dispense::RESULT_NO_WEIGHT;  // dry reservoir or blocked tube
            break;
        }
        if (elapsed >= budgetMs) break;  // too slow: the weighing below reports the shortfall
    }
    pump_.set(false);  // the only place the pump is switched off during a dispense: every exit passes here

    report.pumpMs = millis() - start;
    report.grams = grams;
    emit(DispenseEvent::PUMPED, report);
    return outcome;
}

dispense::ResultCode DispenseSequence::runLiquid(uint8_t ml, DispenseReport &report)
{
    closeShieldAndTare(report);

    // 3. pump into the weighed cup
    report.expectedG = dispense::liquidTargetG(ml, report.unitG);
    report.toleranceG = dispense::liquidTolerance(report.expectedG);
    emit(DispenseEvent::PUMPING, report);
    const dispense::ResultCode pumped = pumpToWeight(ml, report);
    if (pumped != dispense::RESULT_OK) return fail(pumped, report);

    // 4. let the tube drip, then the weight must settle and match
    pollFor(board::LIQUID_DRIP_MS);
    emit(DispenseEvent::WEIGHT_WAITING, report);
    report.steady = scale_.steadyGrams(report.grams, hook_);
    emit(DispenseEvent::WEIGHT_MEASURED, report);

    emit(DispenseEvent::WEIGHT_COMPARING, report);
    if (!dispense::liquidMatches(report.grams, report.expectedG))
        return fail(dispense::RESULT_WEIGHT_MISMATCH, report);
    emit(DispenseEvent::WEIGHT_MATCHED, report);

    // 5. open the shield
    return openShieldAndPass(report);
}
