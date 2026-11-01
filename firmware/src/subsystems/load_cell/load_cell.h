#pragma once

#include <Arduino.h>

class LoadCell
{
public:
    void begin(uint8_t dataPin, uint8_t clockPin);
    bool available() const;
    long read() const;

private:
    uint8_t dataPin_ = 0;
    uint8_t clockPin_ = 0;
};
