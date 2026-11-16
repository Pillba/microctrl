#include "debug_log.h"

#if PILLBA_DEBUG_LOG

#include <board.h>
#include <dispense_rules.h>
#include <shield.h>

namespace debug_log
{
namespace
{
void start()
{
    Serial.print(F("DBG "));
}

void printId(const uint8_t *requestId)
{
    for (uint8_t i = 0; i < protocol::REQUEST_ID_LENGTH; ++i)
    {
        if (requestId[i] < 0x10) Serial.print('0');
        Serial.print(requestId[i], HEX);
    }
}

void printHexByte(uint8_t value)
{
    Serial.print(F("0x"));
    if (value < 0x10) Serial.print('0');
    Serial.print(value, HEX);
}

void printGrams(float grams)
{
    Serial.print(grams, 3);
    Serial.print(F(" g"));
}

const __FlashStringHelper *resultName(uint8_t code)
{
    switch (code)
    {
    case dispense::RESULT_OK: return F("OK");
    case dispense::RESULT_FAILED: return F("FAILED");
    case dispense::RESULT_INVALID_REQUEST: return F("INVALID_REQUEST");
    case dispense::RESULT_BUSY: return F("BUSY");
    case dispense::RESULT_SCALE_NOT_RESPONDING: return F("SCALE_NOT_RESPONDING");
    case dispense::RESULT_IR_NOT_TRIPPED: return F("IR_NOT_TRIPPED");
    case dispense::RESULT_NO_WEIGHT: return F("NO_WEIGHT");
    case dispense::RESULT_WEIGHT_MISMATCH: return F("WEIGHT_MISMATCH");
    default: return F("UNKNOWN");
    }
}

const __FlashStringHelper *benchStatusName(uint8_t status)
{
    switch (status)
    {
    case protocol::BENCH_STATUS_OK: return F("OK");
    case protocol::BENCH_STATUS_FAILED: return F("FAILED (HX711 does not answer)");
    case protocol::BENCH_STATUS_INVALID: return F("INVALID (no START first)");
    case protocol::BENCH_STATUS_BUSY: return F("BUSY");
    case protocol::BENCH_STATUS_UNSTEADY: return F("SAVED, readings unsteady");
    case protocol::BENCH_STATUS_CANCELLED: return F("CANCELLED");
    case protocol::BENCH_STATUS_NO_CHANGE: return F("NO_CHANGE (nothing saved)");
    default: return F("UNKNOWN");
    }
}

const __FlashStringHelper *calibrationName(uint8_t command)
{
    return command == protocol::BENCH_CALIBRATE_START ? F("CALIBRATE_START") : F("CALIBRATE_FINISH");
}

const __FlashStringHelper *decisionName(protocol::DispatchDecision decision)
{
    switch (decision)
    {
    case protocol::DISPATCH_ACCEPT: return F("accepted");
    case protocol::DISPATCH_REJECT_BUSY: return F("rejected: BUSY");
    case protocol::DISPATCH_REJECT_INVALID: return F("rejected: INVALID_REQUEST");
    default: return F("ignored");
    }
}
}  // namespace

void boot(bool scaleReady, bool calibrated, float factor)
{
    start();
    Serial.println(F("production firmware ready (DEBUG build: text lines between frames)"));
    start();
    if (!scaleReady) Serial.println(F("scale: the HX711 does not answer, every dispense will fail"));
    else if (!calibrated) Serial.println(F("scale: NOT calibrated on this board, run calibration first"));
    else
    {
        Serial.print(F("scale: calibration factor "));
        Serial.println(factor, 2);
    }
    start();
    Serial.println(Shield::sensorsFitted() ? F("shield: hall sensors fitted")
                                           : F("shield: no hall sensors, moves are timed"));
}

void dispenseCommand(const protocol::DispenseCommand &command, protocol::DispatchDecision decision)
{
    start();
    Serial.print(F("DISPENSE_CMD id "));
    printId(command.requestId);
    Serial.print(F(" slot "));
    Serial.print(command.slot);
    Serial.print(dispense::isLiquidSlot(command.slot) ? F(" mL ") : F(" qty "));
    Serial.print(command.quantity);
    Serial.print(F(", unit "));
    if (command.unitMg) { Serial.print(command.unitMg); Serial.print(F(" mg")); }
    else Serial.print(F("default"));
    Serial.print(F(": "));
    Serial.println(decisionName(decision));
}

void calibrationCommand(const protocol::BenchCommand &command, protocol::DispatchDecision decision)
{
    start();
    Serial.print(calibrationName(command.command));
    Serial.print(F(" id "));
    printId(command.requestId);
    if (command.command == protocol::BENCH_CALIBRATE_FINISH)
    {
        Serial.print(F(" known weight "));
        printGrams(command.arg / 1000000.0);
    }
    Serial.print(F(": "));
    Serial.println(decisionName(decision));
}

void ignoredFrame(const protocol::Frame &frame)
{
    start();
    if (frame.type == protocol::MSG_BENCH_CMD && frame.length == protocol::BENCH_CMD_LENGTH)
    {
        Serial.print(F("BENCH_CMD "));
        printHexByte(frame.payload[protocol::REQUEST_ID_LENGTH]);
        Serial.println(F(" ignored: only calibration (0x08/0x09) runs in production, flash dispense_UI for the rest"));
        return;
    }
    Serial.print(F("frame type "));
    printHexByte(frame.type);
    Serial.print(F(" length "));
    Serial.print(frame.length);
    Serial.println(F(" ignored: not a command production handles"));
}

void dispenseEvent(DispenseEvent event, const DispenseReport &report)
{
    switch (event)
    {
    case DispenseEvent::STARTED:
        start();
        Serial.print(F("dispense started, slot "));
        Serial.print(report.slot);
        Serial.print(dispense::isLiquidSlot(report.slot) ? F(", mL ") : F(", qty "));
        Serial.print(report.quantity);
        Serial.print(dispense::isLiquidSlot(report.slot) ? F(", g per mL ") : F(", g per pill "));
        Serial.println(report.unitG, 3);
        break;
    case DispenseEvent::SHIELD_CLOSING:
        start();
        Serial.println(F("closing the shield"));
        break;
    case DispenseEvent::SHIELD_CLOSED:
        start();
        Serial.println(report.shieldConfirmed ? F("shield closed (hall confirms)")
                                              : F("shield commanded closed (not confirmed)"));
        break;
    case DispenseEvent::TARING:
        start();
        Serial.println(F("taring the scale"));
        break;
    case DispenseEvent::TARED:
        start();
        Serial.println(F("scale tared"));
        break;
    case DispenseEvent::STEPPING:
        start();
        Serial.print(F("stepper "));
        Serial.print(report.slot);
        Serial.print(F(": stepping until "));
        Serial.print(report.quantity);
        Serial.print(F(" IR breaks, at most "));
        Serial.print(report.steps);
        Serial.println(F(" steps"));
        break;
    case DispenseEvent::STEPPED:
        start();
        Serial.print(F("stepper stopped after "));
        Serial.print(report.steps);
        Serial.println(F(" steps"));
        break;
    case DispenseEvent::IR_CHECKING:
        start();
        Serial.println(F("waiting for IR beam breaks"));
        break;
    case DispenseEvent::IR_CONFIRMED:
        start();
        Serial.print(F("IR beam breaks: "));
        Serial.println(report.trips);
        break;
    case DispenseEvent::PUMPING:
        start();
        Serial.print(F("pumping to "));
        printGrams(report.expectedG);
        Serial.println();
        break;
    case DispenseEvent::PUMPED:
        start();
        Serial.print(F("pump off after "));
        Serial.print(report.pumpMs);
        Serial.print(F(" ms at "));
        printGrams(report.grams);
        Serial.println();
        break;
    case DispenseEvent::WEIGHT_WAITING:
        start();
        Serial.println(F("waiting for a steady weight"));
        break;
    case DispenseEvent::WEIGHT_MEASURED:
        start();
        Serial.print(report.steady ? F("weight ") : F("weight (never steady) "));
        printGrams(report.grams);
        Serial.println();
        break;
    case DispenseEvent::WEIGHT_COMPARING:
        start();
        Serial.print(F("expected "));
        printGrams(report.expectedG);
        Serial.print(F(" +/- "));
        printGrams(report.toleranceG);
        Serial.println();
        break;
    case DispenseEvent::WEIGHT_MATCHED:
        start();
        Serial.println(F("weight matches"));
        break;
    case DispenseEvent::SHIELD_OPENING:
        start();
        Serial.println(F("opening the shield"));
        break;
    case DispenseEvent::SHIELD_OPENED:
        start();
        Serial.println(report.shieldConfirmed ? F("shield open (hall confirms)")
                                              : F("shield commanded open (not confirmed)"));
        break;
    case DispenseEvent::PASSED:
        start();
        Serial.println(F("dispense passed"));
        break;
    case DispenseEvent::FAILED:
        start();
        Serial.print(F("dispense failed: "));
        Serial.print(resultName(report.code));
        Serial.println(F(", the shield stays closed"));
        break;
    }
}

void dispenseResult(const protocol::DispenseCommand &command, uint8_t code, uint8_t confirmed)
{
    start();
    Serial.print(F("DISPENSE_RESULT id "));
    printId(command.requestId);
    Serial.print(F(" slot "));
    Serial.print(command.slot);
    Serial.print(F(": "));
    Serial.print(code);
    Serial.print(' ');
    Serial.print(resultName(code));
    Serial.print(F(", confirmed "));
    Serial.println(confirmed);
}

void calibrationResult(const protocol::BenchCommand &command, uint8_t status, int32_t factorX100)
{
    start();
    Serial.print(calibrationName(command.command));
    Serial.print(F(" result: "));
    Serial.print(benchStatusName(status));
    Serial.print(F(", factor "));
    Serial.println(factorX100 / 100.0, 2);
}

void calibrationExpired()
{
    start();
    Serial.println(F("calibration session expired (no FINISH): previous factor kept"));
}
}  // namespace debug_log

#endif  // PILLBA_DEBUG_LOG
