#include "configuration.h"

const PinMap &pins()
{
    static const PinMap configuredPins{
        2,  // IR receiver
        12, // IR emitter
        3,  // Hall sensor
        6,  // Pump
        8,  // Step
        9,  // Servo
        10, // Direction
        13, // Stepper reset
        7,  // Load-cell data
        11  // Load-cell clock
    };
    return configuredPins;
}
