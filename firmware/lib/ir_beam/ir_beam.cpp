#include "ir_beam.h"

// The ISRs live in this file so the linker pulls them in together with IrBeam::begin().
namespace
{
volatile uint8_t isrTrippedLevel = LOW;
volatile unsigned long irBreaks = 0;
volatile uint32_t lastTripUs = 0;

inline void onBeamChange()
{
    if (digitalRead(board::IR_PIN) == isrTrippedLevel)
    {
        const uint32_t now = micros();
        if ((uint32_t)(now - lastTripUs) > board::IR_MIN_GAP_US)
        {
            lastTripUs = now;
            irBreaks++;
        }
    }
}
} // namespace

ISR(PCINT0_vect) { onBeamChange(); }
ISR(PCINT1_vect) { onBeamChange(); }
ISR(PCINT2_vect) { onBeamChange(); }

void IrBeam::begin()
{
    pinMode(board::IR_PIN, INPUT);  // the pull up and pull down are on the PCB

    unsigned int highs = 0, lows = 0;
    const unsigned long start = millis();
    while (millis() - start < board::IR_LEARN_MS)
    {
        if (digitalRead(board::IR_PIN) == HIGH) highs++;
        else lows++;
        delayMicroseconds(200);
    }
    trippedLevel_ = (highs >= lows) ? LOW : HIGH;

    noInterrupts();
    isrTrippedLevel = trippedLevel_;
    irBreaks = 0;
    interrupts();
    *digitalPinToPCMSK(board::IR_PIN) |= bit(digitalPinToPCMSKbit(board::IR_PIN));
    *digitalPinToPCICR(board::IR_PIN) |= bit(digitalPinToPCICRbit(board::IR_PIN));
}

unsigned long IrBeam::breaks() const
{
    noInterrupts();
    const unsigned long n = irBreaks;
    interrupts();
    return n;
}
