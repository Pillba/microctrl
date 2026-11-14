#pragma once

// Pin map and hardware timing for the Pillba ATmega328P PCB.
// Values come from firmware/bench/dispense/src/dispense.cpp, which is validated on hardware.
// Change a pin or timing here only after re-validating it on the bench.

#include <Arduino.h>

namespace board
{
// Stepper drivers (DRV8805, six boards sharing STEP and DIR, selected with active-low nENBL)
constexpr uint8_t STEP_PIN = 8;
constexpr uint8_t DIR_PIN = 10;
constexpr uint8_t STEPPER_RESET_PIN = 13;  // DRV8805 RESET, active high
constexpr uint8_t STEPPER_COUNT = 6;
constexpr uint8_t NENBL1_PIN = A0;
constexpr uint8_t NENBL2_PIN = A1;
constexpr uint8_t NENBL3_PIN = A2;
constexpr uint8_t NENBL4_PIN = A3;
constexpr uint8_t NENBL5_PIN = A4;
constexpr uint8_t NENBL6_PIN = A5;

// Shield: servo plus two hall sensors
constexpr uint8_t SERVO_PIN = 9;
constexpr uint8_t HALL_CLOSED_PIN = 3;  // HALL1, detects the shield closed
constexpr uint8_t HALL_OPEN_PIN = 4;    // HALL2, detects the shield open

// Pump 2 driver (PUMP2)
constexpr uint8_t PUMP_PIN = 6;

// IR break beam receiver signal (pull up and pull down are on the PCB, no enable pin)
constexpr uint8_t IR_PIN = 2;

// HX711 load cell
constexpr uint8_t LOAD_CELL_DOUT_PIN = 7;
constexpr uint8_t LOAD_CELL_SCK_PIN = 11;

// Stepper timing
constexpr uint16_t STEPS_PER_PILL = 1000;
constexpr uint16_t STEP_PERIOD_US = 2000;
constexpr uint16_t STEP_PULSE_US = 20;
constexpr uint8_t STEPPER_ENABLE_SETTLE_MS = 5;
constexpr uint8_t STEPS_PER_POLL = 50;  // a poll hook runs this often while stepping

// Shield
constexpr uint8_t SHIELD_CLOSED_ANGLE = 0;
constexpr uint8_t SHIELD_OPEN_ANGLE = 90;
constexpr uint8_t HALL_ACTIVE_LEVEL = LOW;
constexpr uint16_t HALL_DEBOUNCE_MS = 20;
constexpr unsigned long SHIELD_WAIT_MS = 4000;  // how long a hall sensor gets to confirm a move
constexpr uint16_t SHIELD_SETTLE_MS = 300;      // servo travel after the hall sensor confirms

// Pump
constexpr uint8_t PUMP_ON_LEVEL = HIGH;

// IR break beam
constexpr uint32_t IR_MIN_GAP_US = 5000;
constexpr unsigned long IR_WAIT_MS = 5000;    // after the stepper stops, how long a pill may still take
constexpr uint16_t IR_SETTLE_MS = 200;        // a second pill right behind the first shows up here
constexpr uint16_t IR_LEARN_MS = 500;         // clear-level learning at power up

// Load cell
constexpr uint8_t AVERAGE_SCANS = 20;          // scans averaged for a reported weight
constexpr uint8_t CHECK_SCANS = 5;             // scans averaged for each quick check
constexpr uint8_t STARTUP_TARE_SCANS = 30;
constexpr float STEADY_BAND_G = 0.020;         // the last 3 checks must agree within this
constexpr unsigned long WEIGHT_WAIT_MS = 5000; // how long to wait for any weight to arrive
constexpr unsigned long STEADY_WAIT_MS = 15000;
constexpr unsigned long SCALE_READY_TIMEOUT_MS = 1000;
constexpr float DEFAULT_CALIBRATION_FACTOR = 420.00;

// EEPROM layout (shared by every app, so a calibration made in bench_console is used by production)
constexpr int EEPROM_ADDR_MARKER = 0;
constexpr int EEPROM_ADDR_FACTOR = 1;
constexpr uint8_t EEPROM_MARKER = 0x42;

// Sound the Pi plays after a verified dispense
constexpr uint8_t DISPENSE_SOUND_NUMBER = 1;
} // namespace board

// Called from long blocking operations so the app can service the UART.
// Return true to ask the operation to stop early (only StepperBank::run honours this).
typedef bool (*PollHook)();
