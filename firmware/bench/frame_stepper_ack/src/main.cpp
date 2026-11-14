#include <Arduino.h>

namespace
{
constexpr uint8_t FRAME_SYNC_0 = 0xAA;
constexpr uint8_t FRAME_SYNC_1 = 0x55;
constexpr uint8_t DISPENSE_CMD = 0x10;
constexpr uint8_t DISPENSE_RESULT = 0x11;
constexpr uint8_t MAX_PAYLOAD_LENGTH = 64;

constexpr uint8_t STEP_PIN = 8;
constexpr uint8_t DIRECTION_PIN = 10;
constexpr uint8_t STEPPER_RESET_PIN = 13;
constexpr uint8_t ENABLE_PINS[] = {A0, A1, A2, A3, A4, A5};
constexpr uint16_t STEPS_PER_ITEM = 1000;
constexpr uint16_t STEP_HIGH_US = 20;
constexpr uint16_t STEP_LOW_US = 1980;

uint8_t frameBuffer[2 + 2 + MAX_PAYLOAD_LENGTH + 1];
uint8_t frameLength = 0;
bool dispenseActive = false;
bool stepSignalHigh = false;
uint32_t remainingSteps = 0;
uint32_t nextStepTransitionUs = 0;
uint8_t activeRequestId[4] = {};
uint8_t activeSlot = 0;
uint8_t activeQuantity = 0;

uint8_t crc8Maxim(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0;
    for (uint8_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit)
        {
            crc = (crc & 1) ? (crc >> 1) ^ 0x8C : (crc >> 1);
        }
    }
    return crc;
}

void disableAllSteppers()
{
    for (uint8_t pin : ENABLE_PINS)
    {
        digitalWrite(pin, HIGH);
    }
}

void enableStepper(uint8_t slot)
{
    disableAllSteppers();
    digitalWrite(ENABLE_PINS[slot - 1], LOW);
}

void sendResult(
    const uint8_t *requestId,
    uint8_t slot,
    uint8_t resultCode,
    uint8_t confirmedQuantity
)
{
    uint8_t header[] = {
        DISPENSE_RESULT,
        7,
        requestId[0],
        requestId[1],
        requestId[2],
        requestId[3],
        slot,
        resultCode,
        confirmedQuantity,
    };
    Serial.write(FRAME_SYNC_0);
    Serial.write(FRAME_SYNC_1);
    Serial.write(header, sizeof(header));
    Serial.write(crc8Maxim(header, sizeof(header)));
}

void startDispense(const uint8_t *payload)
{
    const uint8_t slot = payload[4];
    const uint8_t quantity = payload[5];

    if (slot < 1 || slot > 6 || quantity == 0)
    {
        sendResult(payload, slot, 2, 0);
        return;
    }
    if (dispenseActive)
    {
        sendResult(payload, slot, 3, 0);
        return;
    }

    for (uint8_t i = 0; i < sizeof(activeRequestId); ++i)
    {
        activeRequestId[i] = payload[i];
    }
    activeSlot = slot;
    activeQuantity = quantity;
    remainingSteps = static_cast<uint32_t>(quantity) * STEPS_PER_ITEM;
    dispenseActive = true;
    stepSignalHigh = false;
    digitalWrite(DIRECTION_PIN, HIGH);
    enableStepper(slot);
    nextStepTransitionUs = micros();
}

void handleFrame(uint8_t messageType, const uint8_t *payload, uint8_t payloadLength)
{
    if (messageType == DISPENSE_CMD && payloadLength == 6)
    {
        startDispense(payload);
    }
}

void pollFramedSerial()
{
    while (Serial.available() > 0)
    {
        const uint8_t received = static_cast<uint8_t>(Serial.read());
        if (frameLength == 0 && received != FRAME_SYNC_0)
        {
            continue;
        }
        if (frameLength == 1 && received != FRAME_SYNC_1)
        {
            frameLength = received == FRAME_SYNC_0 ? 1 : 0;
            continue;
        }

        frameBuffer[frameLength++] = received;
        if (frameLength < 4)
        {
            continue;
        }

        const uint8_t payloadLength = frameBuffer[3];
        if (payloadLength > MAX_PAYLOAD_LENGTH)
        {
            frameLength = 0;
            continue;
        }

        const uint8_t expectedFrameLength = payloadLength + 5;
        if (frameLength < expectedFrameLength)
        {
            continue;
        }

        const uint8_t expectedCrc = crc8Maxim(&frameBuffer[2], payloadLength + 2);
        if (frameBuffer[expectedFrameLength - 1] == expectedCrc)
        {
            handleFrame(frameBuffer[2], &frameBuffer[4], payloadLength);
        }
        frameLength = 0;
    }
}

void updateStepper()
{
    if (!dispenseActive || static_cast<int32_t>(micros() - nextStepTransitionUs) < 0)
    {
        return;
    }

    if (!stepSignalHigh)
    {
        if (remainingSteps == 0)
        {
            disableAllSteppers();
            dispenseActive = false;
            sendResult(activeRequestId, activeSlot, 0, activeQuantity);
            return;
        }

        digitalWrite(STEP_PIN, HIGH);
        stepSignalHigh = true;
        --remainingSteps;
        nextStepTransitionUs = micros() + STEP_HIGH_US;
        return;
    }

    digitalWrite(STEP_PIN, LOW);
    stepSignalHigh = false;
    nextStepTransitionUs = micros() + STEP_LOW_US;
}
} // namespace

void setup()
{
    Serial.begin(115200);

    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIRECTION_PIN, OUTPUT);
    pinMode(STEPPER_RESET_PIN, OUTPUT);
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIRECTION_PIN, HIGH);
    digitalWrite(STEPPER_RESET_PIN, LOW);

    for (uint8_t pin : ENABLE_PINS)
    {
        pinMode(pin, OUTPUT);
    }
    disableAllSteppers();
}

void loop()
{
    pollFramedSerial();
    updateStepper();
}
