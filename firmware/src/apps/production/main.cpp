// Pillba production firmware for the ATmega328P PCB: framed UART (protocol/protocol.md) at 115200.
//
// DISPENSE_CMD (request_id[4] slot quantity [unit_weight_mg]) runs one verified dispense and is answered with
// DISPENSE_RESULT (request_id[4] slot result_code confirmed_quantity). Slots 1-6 are pill steppers;
// slot 7 is the liquid pump and its quantity is mL. While a dispense runs, further commands are
// answered with BUSY right away. Only frames are written to the UART, unless the firmware was built
// with DEBUG=1 (debug_log.h): then "DBG ..." progress lines are written between frames too.
//
// Scale calibration (the panel's Calibrate Scale page, or bench_menu_frames.py "c") uses two BENCH_CMD
// commands with the same payloads as firmware/bench/dispense_UI:
//   CALIBRATE_START   the scale is empty: zero it in raw mode (about 10 s)
//   CALIBRATE_FINISH  arg = known weight in micrograms (0 cancels): measure, save the factor in EEPROM
// Between START and FINISH the scale reads raw values, so dispense commands are answered BUSY. A
// session that is not finished within CALIBRATION_IDLE_MS is cancelled (previous factor kept).
// Every other BENCH_CMD command is ignored.
//
// The calibration factor lives in EEPROM, shared with bench_console. Program the EESAVE fuse once
// (firmware/README.md) so flashing keeps it.

#include <Arduino.h>

#include <board.h>
#include <dispense_rules.h>
#include <dispense_sequence.h>
#include <ir_beam.h>
#include <protocol.h>
#include <pump.h>
#include <scale.h>
#include <shield.h>
#include <stepper_bank.h>

#include "debug_log.h"

namespace
{
const uint32_t UART_BAUD = 115200;
const unsigned long CALIBRATION_IDLE_MS = 600000UL;  // 10 minutes from START to FINISH

StepperBank steppers;
Shield shield;
IrBeam ir;
Scale scale;
Pump pump;
DispenseSequence dispenser(steppers, shield, ir, scale, pump);

protocol::FrameParser parser;
protocol::DispenseCommand pending;
bool hasPending = false;
bool dispensing = false;

protocol::BenchCommand pendingCalibration;
bool hasPendingCalibration = false;
bool calibrationStarted = false;  // between a successful START and FINISH: the scale reads raw values
unsigned long calibrationStartedAt = 0;

// A calibration step may not interrupt a dispense or another queued step.
bool calibrationBusy()
{
    return dispensing || hasPending || hasPendingCalibration;
}

// A dispense may not run while a calibration step is queued or a session is open.
bool dispenseBusy()
{
    return calibrationBusy() || calibrationStarted;
}

void sendDispenseResult(const protocol::DispenseCommand &command, uint8_t resultCode,
                        uint8_t confirmedQuantity)
{
    uint8_t frame[protocol::DISPENSE_RESULT_LENGTH + protocol::FRAME_OVERHEAD];
    const uint8_t length =
        protocol::encodeDispenseResult(command, resultCode, confirmedQuantity, frame, sizeof(frame));
    Serial.write(frame, length);
}

void sendBenchResult(const protocol::BenchCommand &command, uint8_t status, int32_t value)
{
    uint8_t frame[protocol::BENCH_RESULT_LENGTH + protocol::FRAME_OVERHEAD];
    const uint8_t length = protocol::encodeBenchResult(command, status, value, frame, sizeof(frame));
    Serial.write(frame, length);
}

void handleFrame(const protocol::Frame &frame)
{
    protocol::BenchCommand bench;
    const protocol::DispatchDecision calibration = protocol::decideCalibration(frame, calibrationBusy(), bench);
    if (calibration != protocol::DISPATCH_IGNORE) debug_log::calibrationCommand(bench, calibration);
    switch (calibration)
    {
    case protocol::DISPATCH_REJECT_BUSY:
        sendBenchResult(bench, protocol::BENCH_STATUS_BUSY, 0);
        return;
    case protocol::DISPATCH_ACCEPT:
        pendingCalibration = bench;
        hasPendingCalibration = true;
        return;
    default:
        break;  // not a calibration command
    }

    protocol::DispenseCommand command;
    const protocol::DispatchDecision decision = protocol::decideDispense(frame, dispenseBusy(), command);
    if (decision == protocol::DISPATCH_IGNORE) debug_log::ignoredFrame(frame);
    else debug_log::dispenseCommand(command, decision);
    switch (decision)
    {
    case protocol::DISPATCH_IGNORE:
        break;
    case protocol::DISPATCH_REJECT_INVALID:
        sendDispenseResult(command, dispense::RESULT_INVALID_REQUEST, 0);
        break;
    case protocol::DISPATCH_REJECT_BUSY:
        sendDispenseResult(command, dispense::RESULT_BUSY, 0);
        break;
    case protocol::DISPATCH_ACCEPT:
        pending = command;
        hasPending = true;
        break;
    }
}

// Services the UART and hall sensors. Used by the main loop and as the dispense poll hook,
// so commands that arrive during a dispense get an immediate BUSY.
bool poll()
{
    while (Serial.available() > 0)
    {
        protocol::Frame frame;
        if (parser.feed(static_cast<uint8_t>(Serial.read()), frame)) handleFrame(frame);
    }
    shield.update();
    return false;
}

void runPending()
{
    const protocol::DispenseCommand command = pending;
    hasPending = false;
    dispensing = true;
    DispenseReport report;
    const dispense::ResultCode code =
        dispenser.run(command.slot, command.quantity, report, command.unitMg / 1000.0f);
    debug_log::dispenseResult(command, code, report.confirmedQuantity);
    sendDispenseResult(command, code, report.confirmedQuantity);
    dispensing = false;
}

int32_t factorX100()
{
    return (int32_t)(scale.factor() * 100.0);
}

void endCalibrationSession()
{
    if (!calibrationStarted) return;
    scale.cancelCalibration();  // back to the saved factor
    calibrationStarted = false;
}

// Blocking (about 10 s for START, 35 s for FINISH). Frames that arrive meanwhile wait in the UART
// buffer; the panel holds dispensing for the whole calibration session anyway.
void runPendingCalibration()
{
    const protocol::BenchCommand command = pendingCalibration;
    hasPendingCalibration = false;

    if (command.command == protocol::BENCH_CALIBRATE_START)
    {
        uint8_t status = protocol::BENCH_STATUS_OK;
        if (scale.ready())
        {
            scale.startCalibration();
            calibrationStarted = true;
            calibrationStartedAt = millis();
        }
        else
        {
            endCalibrationSession();
            status = protocol::BENCH_STATUS_FAILED;
        }
        debug_log::calibrationResult(command, status, factorX100());
        sendBenchResult(command, status, 0);
        return;
    }

    // CALIBRATE_FINISH
    uint8_t status;
    if (!calibrationStarted)
    {
        status = protocol::BENCH_STATUS_INVALID;
    }
    else if (command.arg == 0)
    {
        endCalibrationSession();
        status = protocol::BENCH_STATUS_CANCELLED;
    }
    else if (!scale.ready())
    {
        endCalibrationSession();
        status = protocol::BENCH_STATUS_FAILED;
    }
    else
    {
        calibrationStarted = false;
        switch (scale.finishCalibration(command.arg / 1000000.0))
        {
        case Scale::CALIBRATION_SAVED:
            status = protocol::BENCH_STATUS_OK;
            break;
        case Scale::CALIBRATION_SAVED_UNSTEADY:
            status = protocol::BENCH_STATUS_UNSTEADY;
            break;
        default:  // CALIBRATION_NO_CHANGE: previous factor restored
            status = protocol::BENCH_STATUS_NO_CHANGE;
            break;
        }
    }
    debug_log::calibrationResult(command, status, factorX100());
    sendBenchResult(command, status, factorX100());
}
} // namespace

void setup()
{
    Serial.begin(UART_BAUD);

    pump.begin();
    steppers.begin();
    shield.begin();
    const bool scaleReady = scale.begin();  // if the HX711 is missing, every dispense reports SCALE_NOT_RESPONDING
    ir.begin();
    dispenser.setPollHook(poll);
    if (PILLBA_DEBUG_LOG) dispenser.setListener(debug_log::dispenseEvent);
    debug_log::boot(scaleReady, scale.calibrated(), scale.factor());
}

void loop()
{
    poll();
    if (hasPendingCalibration) runPendingCalibration();
    else if (hasPending) runPending();

    if (calibrationStarted && millis() - calibrationStartedAt >= CALIBRATION_IDLE_MS)
    {
        endCalibrationSession();  // the panel went away mid-session: back to the saved factor
        debug_log::calibrationExpired();
    }
}
