#include "shield.h"

namespace
{
// Runs hook (if any) for ms milliseconds.
void waitWithHook(unsigned long ms, PollHook hook)
{
    const unsigned long start = millis();
    while (millis() - start < ms)
    {
        if (hook) hook();
    }
}
} // namespace

void Shield::begin()
{
    pinMode(board::HALL_CLOSED_PIN, INPUT_PULLUP);
    pinMode(board::HALL_OPEN_PIN, INPUT_PULLUP);
    servo_.attach(board::SERVO_PIN);
    servo_.write(board::SHIELD_CLOSED_ANGLE);
}

bool Shield::closedDetected() const
{
    if (!sensorsFitted()) return false;
    return digitalRead(board::HALL_CLOSED_PIN) == board::HALL_ACTIVE_LEVEL;
}

bool Shield::openDetected() const
{
    if (!sensorsFitted()) return false;
    return digitalRead(board::HALL_OPEN_PIN) == board::HALL_ACTIVE_LEVEL;
}

void Shield::command(bool open)
{
    servo_.write(open ? board::SHIELD_OPEN_ANGLE : board::SHIELD_CLOSED_ANGLE);
}

bool Shield::update()
{
    if (!sensorsFitted()) return false;

    HallState reading = HallState::BETWEEN;
    const bool closed = closedDetected();
    const bool open = openDetected();
    if (closed && open) reading = HallState::ERROR;
    else if (closed) reading = HallState::CLOSED;
    else if (open) reading = HallState::OPEN;

    if (reading != candidate_)
    {
        candidate_ = reading;
        candidateSince_ = millis();
        return false;
    }

    if (reading != state_ && millis() - candidateSince_ >= board::HALL_DEBOUNCE_MS)
    {
        state_ = reading;
        if (handler_) handler_(state_);
        return true;
    }
    return false;
}

bool Shield::move(bool open, unsigned long timeoutMs, PollHook hook)
{
    command(open);
    if (!sensorsFitted())
    {
        waitWithHook(board::SHIELD_TRAVEL_MS, hook);  // nothing can confirm the move
        return false;
    }

    const unsigned long start = millis();
    while (millis() - start < timeoutMs)
    {
        if (hook) hook();
        update();
        if (open ? openDetected() : closedDetected())
        {
            waitWithHook(board::SHIELD_SETTLE_MS, hook);  // let the servo finish its travel
            update();
            return true;
        }
    }
    return false;
}
