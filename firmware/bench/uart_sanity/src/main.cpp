// UART sanity firmware for the Pillba PCB.
//
// Everything that moves or draws current is held off (steppers disabled and their drivers in reset,
// pump off, no servo pulses, HX711 idle), so the only thing running is the UART:
//   - at boot: one banner line with the baud, F_CPU and why the chip reset (POR/EXT/BOD/WDT)
//   - every 500 ms: "ALIVE n up=<ms>" (n restarting at 1 means the MCU reset)
//   - anything received is reported back as "RX <count>: <hex bytes>"
// Plain ASCII, so any terminal at the same baud shows it.

#include <Arduino.h>
#include <avr/io.h>

#ifndef SANITY_BAUD
#define SANITY_BAUD 115200
#endif

namespace
{
constexpr uint8_t STEP_PIN = 8;
constexpr uint8_t DIR_PIN = 10;
constexpr uint8_t STEPPER_RESET_PIN = 13;  // DRV8805 RESET, active high: held high = drivers off
constexpr uint8_t NENBL_PINS[] = {A0, A1, A2, A3, A4, A5};  // active low: high = disabled
constexpr uint8_t SERVO_PIN = 9;
constexpr uint8_t PUMP_PIN = 6;            // active high
constexpr uint8_t LOAD_CELL_SCK_PIN = 11;
constexpr unsigned long ALIVE_PERIOD_MS = 500;

uint8_t resetCause = 0;
uint32_t aliveCount = 0;
unsigned long lastAlive = 0;

void outputLevel(uint8_t pin, uint8_t level)
{
    digitalWrite(pin, level);  // set the level first so the pin never glitches the other way
    pinMode(pin, OUTPUT);
}

void holdEverythingOff()
{
    for (uint8_t pin : NENBL_PINS) outputLevel(pin, HIGH);
    outputLevel(STEPPER_RESET_PIN, HIGH);
    outputLevel(STEP_PIN, LOW);
    outputLevel(DIR_PIN, LOW);
    outputLevel(PUMP_PIN, LOW);
    outputLevel(SERVO_PIN, LOW);
    outputLevel(LOAD_CELL_SCK_PIN, LOW);
}

void printResetCause()
{
    Serial.print(F(" reset="));
    if (resetCause == 0) Serial.print(F("none"));
    if (resetCause & _BV(PORF)) Serial.print(F("POR "));   // power on
    if (resetCause & _BV(EXTRF)) Serial.print(F("EXT "));  // RESET pin pulled low
    if (resetCause & _BV(BORF)) Serial.print(F("BOD "));   // supply dipped below the brown-out level
    if (resetCause & _BV(WDRF)) Serial.print(F("WDT "));   // watchdog
}

void reportReceived()
{
    uint8_t count = 0;
    char line[8];
    while (Serial.available() > 0)
    {
        if (count == 0) Serial.print(F("RX"));
        const uint8_t value = static_cast<uint8_t>(Serial.read());
        snprintf(line, sizeof(line), " %02X", value);
        Serial.print(line);
        if (++count >= 32) break;
    }
    if (count > 0) Serial.println();
}
}  // namespace

void setup()
{
    resetCause = MCUSR;  // no bootloader on this board, so the flags are still here
    MCUSR = 0;
    holdEverythingOff();

    Serial.begin(SANITY_BAUD);
    delay(50);
    Serial.println();
    Serial.print(F("PILLBA UART SANITY baud="));
    Serial.print(static_cast<unsigned long>(SANITY_BAUD));
    Serial.print(F(" f_cpu="));
    Serial.print(static_cast<unsigned long>(F_CPU));
    printResetCause();
    Serial.println();
}

void loop()
{
    reportReceived();

    const unsigned long now = millis();
    if (now - lastAlive >= ALIVE_PERIOD_MS)
    {
        lastAlive = now;
        Serial.print(F("ALIVE "));
        Serial.print(++aliveCount);
        Serial.print(F(" up="));
        Serial.println(now);
    }
}
