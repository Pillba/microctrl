#include "load_cell.h"

void LoadCell::begin(uint8_t dataPin, uint8_t clockPin)
{
    dataPin_ = dataPin;
    clockPin_ = clockPin;
    pinMode(dataPin_, INPUT);
    pinMode(clockPin_, OUTPUT);
    digitalWrite(clockPin_, LOW);
}

bool LoadCell::available() const
{
    return digitalRead(dataPin_) == LOW;
}

long LoadCell::read() const
{
    return 0;
}
