// Pillba bench console for the ATmega328P PCB: every subsystem by hand, plus one command that runs a
// whole dispense. Plain text over the UART at 9600 baud (no frames), for pillba_test_terminal.py on the Pi.
// Same commands and output as the validated reference firmware/bench/dispense/src/dispense.cpp.
//
// Commands:
//   test 1 ... test 6   whole dispense on that stepper (shield, tare, stepper, IR, weight, shield open, sound)
//                       If a check fails, the shield stays closed and it says why.
//   test 7 [ml]         whole liquid dispense (shield, tare, pump until ml * 1.00 g/mL, weight, shield open),
//                       default 5 mL, at most dispense::LIQUID_MAX_ML
//   1 ... 6             run that stepper again and again (1000 steps, pause, repeat)
//   stop                stop the stepper loop
//   9 / 8               open / close the shield (no hall confirmation)
//   pon / poff          pump on / off
//   t                   tare the scale
//   r                   noise test of the scale
//   w                   one weight reading now
//   c                   calibrate the scale ON THIS BOARD: follow the messages, type the known weight in grams
//   e <grams>           set the expected weight for "test" without flashing again, for example  e 0.146
//
// IR break beam: the clear level is learned at power up, so keep the beam CLEAR when the board starts.
// Hall sensors: optional (board::HALL_SENSORS_FITTED). Without them shield moves are not confirmed.

#include <Arduino.h>

#include <board.h>
#include <dispense_rules.h>
#include <dispense_sequence.h>
#include <ir_beam.h>
#include <pump.h>
#include <scale.h>
#include <shield.h>
#include <stepper_bank.h>

#define CONSOLE Serial

namespace
{
const uint32_t UART_BAUD = 9600;
const size_t MAX_MESSAGE_LENGTH = 64;
const uint16_t LOOP_PAUSE_MS = 3000;
const float MG_PER_GRAM = 1000.0;
const uint8_t DEFAULT_TEST_ML = 5;

StepperBank steppers;
Shield shield;
IrBeam ir;
Scale scale;
Pump pump;
DispenseSequence dispenser(steppers, shield, ir, scale, pump);

char message[MAX_MESSAGE_LENGTH];
size_t messageLength = 0;

uint8_t activeBoard = 0;
bool commandChanged = false;

void printGrams(float grams)
{
    CONSOLE.print(grams, 3);
    CONSOLE.print(F(" g ("));
    CONSOLE.print(grams * MG_PER_GRAM, 0);
    CONSOLE.print(F(" mg)"));
}

void sendPlay(uint8_t soundNumber)
{
    CONSOLE.print(F("PLAY:"));
    CONSOLE.println(soundNumber);
}

void pumpLine(bool on)
{
    CONSOLE.println(on ? F("PUMP:on") : F("PUMP:off"));
}

void printServoCommand(bool open)
{
    CONSOLE.println(open ? F("SERVO:cmd open") : F("SERVO:cmd closed"));
}

void printHallChange(HallState state)
{
    if (state == HallState::CLOSED) CONSOLE.println(F("HALL:closed"));
    else if (state == HallState::OPEN) CONSOLE.println(F("HALL:open"));
    else if (state == HallState::ERROR) CONSOLE.println(F("HALL:error, both sensors detect"));
}

void printFailure(const DispenseReport &report)
{
    const __FlashStringHelper *reason = F("the dispense failed.");
    switch (report.code)
    {
    case dispense::RESULT_SCALE_NOT_RESPONDING:
        reason = F("the load cell (HX711) does not answer. Check its wiring.");
        break;
    case dispense::RESULT_IR_NOT_TRIPPED:
        reason = F("the IR beam was not tripped. Nothing fell, or the beam did not see it.");
        break;
    case dispense::RESULT_NO_WEIGHT:
        reason = dispense::isLiquidSlot(report.slot)
                     ? F("no liquid reached the cup. Check the reservoir and the tube.")
                     : F("the IR beam tripped but no weight reached the cup.");
        break;
    case dispense::RESULT_WEIGHT_MISMATCH:
        CONSOLE.print(F("ERROR: Weight out of range. Off by "));
        printGrams(report.grams - report.expectedG);
        CONSOLE.println();
        reason = F("the weight does not match the expected weight.");
        break;
    default:
        break;
    }
    CONSOLE.print(F("RESULT: FAIL, "));
    CONSOLE.println(reason);
    CONSOLE.println(F("TEST: The shield stays closed."));
}

void onDispenseEvent(DispenseEvent event, const DispenseReport &report)
{
    switch (event)
    {
    case DispenseEvent::STARTED:
        if (dispense::isLiquidSlot(report.slot))
        {
            CONSOLE.print(F("TEST: Starting automated liquid dispense of "));
            CONSOLE.print(report.quantity);
            CONSOLE.println(F(" mL"));
        }
        else
        {
            CONSOLE.print(F("TEST: Starting automated dispense on board "));
            CONSOLE.println(report.slot);
        }
        if (!scale.calibrated())
            CONSOLE.println(F("WARNING: The scale is NOT calibrated on this board, so the weight will be wrong. Send c first."));
        break;
    case DispenseEvent::SHIELD_CLOSING:
        CONSOLE.println(F("TEST: Closing the shield..."));
        printServoCommand(false);
        break;
    case DispenseEvent::SHIELD_CLOSED:
        if (report.shieldConfirmed) CONSOLE.println(F("VERIFY: Shield closed, hall sensor confirms."));
        else if (!Shield::sensorsFitted()) CONSOLE.println(F("NOTE: Shield commanded closed (no hall sensors fitted)."));
        else CONSOLE.println(F("WARNING: The hall sensor did NOT confirm that the shield is closed. Carrying on."));
        break;
    case DispenseEvent::TARING:
        CONSOLE.println(F("TEST: Taring the scale..."));
        break;
    case DispenseEvent::TARED:
        CONSOLE.println(F("VERIFY: Scale has tared."));
        break;
    case DispenseEvent::STEPPING:
        CONSOLE.print(F("TEST: Running stepper "));
        CONSOLE.print(report.slot);
        CONSOLE.print(F(" for "));
        CONSOLE.print((uint32_t)board::STEPS_PER_PILL * report.quantity);
        CONSOLE.println(F(" steps..."));
        break;
    case DispenseEvent::STEPPED:
        CONSOLE.println(F("VERIFY: Stepper finished."));
        break;
    case DispenseEvent::IR_CHECKING:
        CONSOLE.println(F("TEST: Checking IR break beam..."));
        break;
    case DispenseEvent::IR_CONFIRMED:
        CONSOLE.print(F("VERIFY: IR beam tripped "));
        CONSOLE.print(report.trips);
        CONSOLE.println(report.trips == 1 ? F(" time.") : F(" times. More than one thing may have fallen."));
        break;
    case DispenseEvent::PUMPING:
        CONSOLE.print(F("TEST: Pumping until the cup gains "));
        printGrams(report.expectedG);
        CONSOLE.println(F("..."));
        pumpLine(true);
        break;
    case DispenseEvent::PUMPED:
        pumpLine(false);
        CONSOLE.print(F("VERIFY: Pump stopped after "));
        CONSOLE.print(report.pumpMs);
        CONSOLE.print(F(" ms at "));
        printGrams(report.grams);
        CONSOLE.println();
        break;
    case DispenseEvent::WEIGHT_WAITING:
        CONSOLE.println(F("TEST: Waiting for the weight to arrive and become steady..."));
        break;
    case DispenseEvent::WEIGHT_MEASURED:
        CONSOLE.print(report.steady ? F("VERIFY: Constant reading received: ")
                                    : F("WARNING: The reading never became steady. Last value: "));
        printGrams(report.grams);
        CONSOLE.println();
        break;
    case DispenseEvent::WEIGHT_COMPARING:
        CONSOLE.print(F("TEST: Expected "));
        printGrams(report.expectedG);
        CONSOLE.print(F(", allowed difference "));
        printGrams(report.toleranceG);
        CONSOLE.println();
        break;
    case DispenseEvent::WEIGHT_MATCHED:
        CONSOLE.println(F("VERIFY: Weight matches the expected weight."));
        break;
    case DispenseEvent::SHIELD_OPENING:
        CONSOLE.println(F("TEST: Dose fully verified! Opening the shield..."));
        printServoCommand(true);
        break;
    case DispenseEvent::SHIELD_OPENED:
        if (report.shieldConfirmed) CONSOLE.println(F("VERIFY: Shield open, hall sensor confirms."));
        else if (!Shield::sensorsFitted()) CONSOLE.println(F("NOTE: Shield commanded open (no hall sensors fitted)."));
        else CONSOLE.println(F("WARNING: The hall sensor did NOT confirm that the shield is open."));
        break;
    case DispenseEvent::PASSED:
        sendPlay(board::DISPENSE_SOUND_NUMBER);
        CONSOLE.println(F("RESULT: PASS. Send 8 to close the shield."));
        break;
    case DispenseEvent::FAILED:
        printFailure(report);
        break;
    }
}

// Waits for one line from the Pi (used by the calibration). Returns false after the timeout.
bool waitForLine(unsigned long timeoutMs)
{
    messageLength = 0;
    const unsigned long start = millis();
    while (millis() - start < timeoutMs)
    {
        while (CONSOLE.available() > 0)
        {
            const char received = static_cast<char>(CONSOLE.read());
            if (received == '\n' || received == '\r')
            {
                if (messageLength > 0)
                {
                    message[messageLength] = '\0';
                    messageLength = 0;
                    return true;
                }
                continue;
            }
            if (messageLength < MAX_MESSAGE_LENGTH - 1) message[messageLength++] = received;
        }
        delay(10);
    }
    messageLength = 0;
    return false;
}

void calibrateScale()
{
    if (!scale.ready())
    {
        CONSOLE.println(F("ERROR: The load cell (HX711) does not answer. Check its wiring."));
        return;
    }
    CONSOLE.println(F("SCALE: Calibration. Remove everything from the scale (the empty cup may stay)."));
    delay(4000);
    CONSOLE.println(F("SCALE: Zeroing, please wait about 10 seconds..."));
    scale.startCalibration();

    CONSOLE.println(F("SCALE: Place the known weight in the CENTER of the scale."));
    CONSOLE.println(F("SCALE: Then type its weight in GRAMS and press Enter (example: 10)."));
    while (CONSOLE.available() > 0) CONSOLE.read();
    const float knownGrams = waitForLine(120000UL) ? atof(message) : 0;
    if (knownGrams <= 0)
    {
        CONSOLE.println(F("SCALE: No valid weight received. Calibration cancelled, nothing changed."));
        scale.cancelCalibration();
        return;
    }

    CONSOLE.println(F("SCALE: Hold still, measuring (about 30 seconds)..."));
    const Scale::CalibrationResult result = scale.finishCalibration(knownGrams);
    if (result == Scale::CALIBRATION_NO_CHANGE)
    {
        CONSOLE.println(F("SCALE: The reading did not change. Is the weight on the scale? Nothing saved."));
        return;
    }
    if (result == Scale::CALIBRATION_SAVED_UNSTEADY)
        CONSOLE.println(F("SCALE: Warning, the readings were not steady. Check the mounting and calibrate again."));
    CONSOLE.print(F("SCALE: New calibration factor (saved on this board): "));
    CONSOLE.println(scale.factor(), 2);
    CONSOLE.println(F("SCALE: Send w to read the weight now, it should show the known weight. Then remove it and send t."));
}

void handleMessage()
{
    message[messageLength] = '\0';
    messageLength = 0;

    if (strcmp_P(message, PSTR("stop")) == 0)
    {
        activeBoard = 0;
        steppers.disableAll();
        commandChanged = true;
        CONSOLE.println(F("STOP:ok"));
    }
    else if (strncmp_P(message, PSTR("test "), 5) == 0)
    {
        const uint8_t boardNum = message[5] - '0';
        uint8_t quantity = 1;
        bool ok = dispense::validSlot(boardNum) && message[6] == '\0';
        if (dispense::isLiquidSlot(boardNum) && (message[6] == '\0' || message[6] == ' '))
        {
            const int ml = (message[6] == ' ') ? atoi(message + 7) : DEFAULT_TEST_ML;
            ok = ml >= 1 && ml <= 255 && dispense::validRequest(boardNum, (uint8_t)ml);
            quantity = ok ? (uint8_t)ml : 0;
        }
        if (ok)
        {
            activeBoard = 0;
            commandChanged = true;
            DispenseReport report;
            dispenser.run(boardNum, quantity, report);
        }
        else
        {
            CONSOLE.print(F("ERROR: Invalid test (use test 1 to test 6, or test 7 <1 to "));
            CONSOLE.print(dispense::LIQUID_MAX_ML);
            CONSOLE.println(F(" mL>)"));
        }
    }
    else if (message[1] == '\0' && message[0] >= '1' && message[0] <= '6')
    {
        activeBoard = message[0] - '0';
        commandChanged = true;
        CONSOLE.print(F("STEP:"));
        CONSOLE.print(activeBoard);
        CONSOLE.println(F(" started"));
    }
    else if (strcmp_P(message, PSTR("9")) == 0)
    {
        shield.command(true);
        printServoCommand(true);
    }
    else if (strcmp_P(message, PSTR("8")) == 0)
    {
        shield.command(false);
        printServoCommand(false);
    }
    else if (strcmp_P(message, PSTR("pon")) == 0)
    {
        pump.set(true);
        CONSOLE.println(F("PUMP:on"));
    }
    else if (strcmp_P(message, PSTR("poff")) == 0)
    {
        pump.set(false);
        CONSOLE.println(F("PUMP:off"));
    }
    else if (strcmp_P(message, PSTR("t")) == 0)
    {
        CONSOLE.println(F("SCALE: Taring scale..."));
        scale.tare(30);
        CONSOLE.println(F("SCALE: Tare complete."));
    }
    else if (strcmp_P(message, PSTR("w")) == 0)
    {
        CONSOLE.print(F("SCALE: Weight "));
        printGrams(scale.grams());
        CONSOLE.println(scale.calibrated() ? F("") : F("  (NOT calibrated, send c)"));
    }
    else if (strcmp_P(message, PSTR("c")) == 0)
    {
        calibrateScale();
    }
    else if (message[0] == 'e' && (message[1] == ' ' || message[1] == '\0'))
    {
        const float grams = (message[1] == ' ') ? atof(message + 2) : 0;
        if (grams > 0) dispenser.setPillWeight(grams);
        CONSOLE.print(F("TEST: Expected weight is "));
        printGrams(dispenser.pillWeight());
        CONSOLE.println(grams > 0 ? F(" (changed, until the next power up)") : F(". To change it send  e <grams>"));
    }
    else if (strcmp_P(message, PSTR("r")) == 0)
    {
        CONSOLE.println(F("SCALE: Running noise test..."));
        const long spread = scale.noiseSpread(50);
        CONSOLE.print(F("SCALE: Raw spread over 50 readings: "));
        CONSOLE.println(spread);
    }
    else
    {
        CONSOLE.print(message);
        CONSOLE.println(F(" from mcu"));
    }
}

void pollSerial()
{
    while (CONSOLE.available() > 0)
    {
        const char received = static_cast<char>(CONSOLE.read());
        if (received == '\n' || received == '\r')
        {
            if (messageLength > 0) handleMessage();
            continue;
        }
        if (messageLength < MAX_MESSAGE_LENGTH - 1) message[messageLength++] = received;
        else messageLength = 0;
    }
    shield.update();
}

// Poll hook for the stepper loop: service the console, stop if a new command arrived.
bool pollAndCheckChanged()
{
    pollSerial();
    return commandChanged;
}

void runCycle()
{
    commandChanged = false;
    steppers.run(activeBoard, board::STEPS_PER_PILL, pollAndCheckChanged);
    if (commandChanged) return;

    sendPlay(board::DISPENSE_SOUND_NUMBER);

    const unsigned long start = millis();
    while (millis() - start < LOOP_PAUSE_MS)
    {
        pollSerial();
        if (commandChanged) return;
    }
}
} // namespace

void setup()
{
    CONSOLE.begin(UART_BAUD);

    pump.begin();
    steppers.begin();
    shield.onHallChange(printHallChange);
    shield.begin();
    const bool scaleReady = scale.begin();
    if (!scaleReady) CONSOLE.println(F("ERROR: The load cell (HX711) does not answer. Check its wiring."));
    ir.begin();
    dispenser.setListener(onDispenseEvent);

    if (!Shield::sensorsFitted())
        CONSOLE.println(F("SETUP: Hall sensors not fitted, shield moves are not confirmed."));
    CONSOLE.print(F("SETUP: IR beam, a clear beam reads "));
    CONSOLE.println(ir.clearReadsHigh() ? F("HIGH.") : F("LOW."));
    if (scale.calibrated())
    {
        CONSOLE.print(F("SETUP: Scale factor loaded from this board: "));
        CONSOLE.println(scale.factor(), 2);
    }
    else
    {
        CONSOLE.println(F("SETUP: WARNING the scale is NOT calibrated on this board. Send c before using test."));
    }
    CONSOLE.print(F("SETUP: Expected weight for test is "));
    printGrams(dispenser.pillWeight());
    CONSOLE.println(F(". Change it with  e <grams>"));
    CONSOLE.println(F("SETUP: System initialized and ready."));
}

void loop()
{
    pollSerial();
    if (activeBoard != 0) runCycle();
}
