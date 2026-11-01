#pragma once

#include <Arduino.h>

struct PinMap
{
    uint8_t irReceiver;
    uint8_t irEmitter;
    uint8_t hallSensor;
    uint8_t pump;
    uint8_t step;
    uint8_t servo;
    uint8_t direction;
    uint8_t stepperReset;
    uint8_t loadCellData;
    uint8_t loadCellClock;
};

const PinMap &pins();
