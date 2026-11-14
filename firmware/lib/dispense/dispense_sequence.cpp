#include "dispense_sequence.h"

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

dispense::ResultCode DispenseSequence::run(uint8_t slot, uint8_t quantity, DispenseReport &report)
{
    report = DispenseReport();
    report.slot = slot;
    report.quantity = quantity;
    emit(DispenseEvent::STARTED, report);

    if (!scale_.ready()) return fail(dispense::RESULT_SCALE_NOT_RESPONDING, report);

    // 1. shield closed, confirmed by the hall sensor
    emit(DispenseEvent::SHIELD_CLOSING, report);
    report.shieldConfirmed = shield_.move(false);
    emit(DispenseEvent::SHIELD_CLOSED, report);

    // 2. tare, so only what this dispense drops is weighed
    emit(DispenseEvent::TARING, report);
    scale_.tare(board::AVERAGE_SCANS);
    emit(DispenseEvent::TARED, report);

    // 3. stepper
    const unsigned long initialBreaks = ir_.breaks();
    emit(DispenseEvent::STEPPING, report);
    steppers_.run(slot, (uint32_t)board::STEPS_PER_PILL * quantity, hook_);
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
    report.steady = scale_.steadyGrams(report.grams);
    emit(DispenseEvent::WEIGHT_MEASURED, report);

    report.expectedG = pillWeightG_ * quantity;
    report.toleranceG = dispense::weightTolerance(report.expectedG);
    emit(DispenseEvent::WEIGHT_COMPARING, report);
    if (!dispense::weightMatches(report.grams, report.expectedG))
        return fail(dispense::RESULT_WEIGHT_MISMATCH, report);
    emit(DispenseEvent::WEIGHT_MATCHED, report);

    // 6. everything checked: open the shield
    emit(DispenseEvent::SHIELD_OPENING, report);
    report.shieldConfirmed = shield_.move(true);
    emit(DispenseEvent::SHIELD_OPENED, report);

    report.code = dispense::RESULT_OK;
    report.confirmedQuantity = quantity;
    emit(DispenseEvent::PASSED, report);
    return dispense::RESULT_OK;
}
