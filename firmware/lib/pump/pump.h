#pragma once

#include <board.h>

class Pump
{
public:
    void begin()
    {
        set(false);
        pinMode(board::PUMP_PIN, OUTPUT);
    }

    void set(bool on)
    {
        on_ = on;
        digitalWrite(board::PUMP_PIN, on ? board::PUMP_ON_LEVEL : !board::PUMP_ON_LEVEL);
    }

    bool on() const { return on_; }

private:
    bool on_ = false;
};
