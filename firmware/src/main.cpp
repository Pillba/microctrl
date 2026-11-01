#include <Arduino.h>

#include "subsystems/configuration/configuration.h"
#include "subsystems/hall_sensor/hall_sensor.h"
#include "subsystems/ir_break_beam/ir_break_beam.h"
#include "subsystems/load_cell/load_cell.h"
#include "subsystems/pump/pump.h"
#include "subsystems/servo/servo.h"
#include "subsystems/stepper/stepper.h"
#include "subsystems/uart_protocol/uart_protocol.h"

namespace
{
StepperController stepper;
ServoController servo;
PumpController pump;
LoadCell loadCell;
IrBreakBeam irBreakBeam;
HallSensor hallSensor;
}

void setup()
{
    const PinMap &pinMap = pins();
    UartProtocol::begin(115200);
    stepper.begin(pinMap.step, pinMap.direction, pinMap.stepperReset);
    servo.begin(pinMap.servo);
    pump.begin(pinMap.pump);
    loadCell.begin(pinMap.loadCellData, pinMap.loadCellClock);
    irBreakBeam.begin(pinMap.irEmitter, pinMap.irReceiver);
    hallSensor.begin(pinMap.hallSensor);
}

void loop()
{
    UartProtocol::poll();
}
