#pragma once

#include <board.h>
#include <Servo.h>

enum class HallState : uint8_t
{
    BETWEEN = 0,
    CLOSED = 1,
    OPEN = 2,
    ERROR = 3,  // both sensors detect
    UNKNOWN = 255,
};

// Called when the debounced hall state changes (the app decides whether to print it).
typedef void (*HallChangeHandler)(HallState state);

// Servo-driven shield with a hall sensor at each end of travel.
class Shield
{
public:
    // Hall inputs with pull ups, servo attached and commanded closed.
    void begin();

    void onHallChange(HallChangeHandler handler) { handler_ = handler; }

    // Commands the servo only; does not wait for the hall sensor.
    void command(bool open);

    // Commands the servo and waits up to timeoutMs for the matching hall sensor.
    // Returns true if the sensor confirmed the move.
    bool move(bool open, unsigned long timeoutMs = board::SHIELD_WAIT_MS);

    // Debounces the hall sensors. Call often. Returns true when the state changed.
    bool update();

    HallState state() const { return state_; }
    bool closedDetected() const;
    bool openDetected() const;

private:
    Servo servo_;
    HallChangeHandler handler_ = nullptr;
    HallState state_ = HallState::UNKNOWN;
    HallState candidate_ = HallState::UNKNOWN;
    unsigned long candidateSince_ = 0;
};
