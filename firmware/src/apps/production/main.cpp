// Pillba production firmware for the ATmega328P PCB: framed UART (protocol/protocol.md) at 115200.
//
// DISPENSE_CMD (request_id[4] slot quantity) runs one verified dispense and is answered with
// DISPENSE_RESULT (request_id[4] slot result_code confirmed_quantity). While a dispense runs, further
// commands are answered with BUSY right away. Only frames are written to the UART.
//
// The scale calibration factor is read from EEPROM; calibrate with the bench_console app first.

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

namespace
{
const uint32_t UART_BAUD = 115200;

StepperBank steppers;
Shield shield;
IrBeam ir;
Scale scale;
Pump pump;
DispenseSequence dispenser(steppers, shield, ir, scale);

protocol::FrameParser parser;
protocol::DispenseCommand pending;
bool hasPending = false;
bool dispensing = false;

void sendDispenseResult(const protocol::DispenseCommand &command, uint8_t resultCode,
                        uint8_t confirmedQuantity)
{
    uint8_t frame[protocol::DISPENSE_RESULT_LENGTH + protocol::FRAME_OVERHEAD];
    const uint8_t length =
        protocol::encodeDispenseResult(command, resultCode, confirmedQuantity, frame, sizeof(frame));
    Serial.write(frame, length);
}

void handleFrame(const protocol::Frame &frame)
{
    protocol::DispenseCommand command;
    switch (protocol::decideDispense(frame, dispensing || hasPending, command))
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
    const dispense::ResultCode code = dispenser.run(command.slot, command.quantity, report);
    sendDispenseResult(command, code, report.confirmedQuantity);
    dispensing = false;
}
} // namespace

void setup()
{
    Serial.begin(UART_BAUD);

    pump.begin();
    steppers.begin();
    shield.begin();
    scale.begin();  // if the HX711 is missing, every dispense reports SCALE_NOT_RESPONDING
    ir.begin();
    dispenser.setPollHook(poll);
}

void loop()
{
    poll();
    if (hasPending) runPending();
}
