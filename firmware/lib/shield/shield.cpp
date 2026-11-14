#include "shield.h"

void Shield::begin()
{
    pinMode(board::HALL_CLOSED_PIN, INPUT_PULLUP);
    pinMode(board::HALL_OPEN_PIN, INPUT_PULLUP);
    servo_.attach(board::SERVO_PIN);
    servo_.write(board::SHIELD_CLOSED_ANGLE);
}

bool Shield::closedDetected() const
{
    return digitalRead(board::HALL_CLOSED_PIN) == board::HALL_ACTIVE_LEVEL;
}

bool Shield::openDetected() const
{
    return digitalRead(board::HALL_OPEN_PIN) == board::HALL_ACTIVE_LEVEL;
}

void Shield::command(bool open)
{
    servo_.write(open ? board::SHIELD_OPEN_ANGLE : board::SHIELD_CLOSED_ANGLE);
}

bool Shield::update()
{
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

bool Shield::move(bool open, unsigned long timeoutMs)
{
    command(open);
    const unsigned long start = millis();
    while (millis() - start < timeoutMs)
    {
        update();
        if (open ? openDetected() : closedDetected())
        {
            delay(board::SHIELD_SETTLE_MS);  // let the servo finish its travel
            update();
            return true;
        }
    }
    return false;
}
