#include <Arduino.h>

// Use Serial1 on boards with a second hardware UART.
// On an Arduino Uno, use Serial or SoftwareSerial instead.
#define DISPENSE_SERIAL Serial

const uint32_t UART_BAUD = 115200;

const uint8_t FRAME_SYNC_1 = 0xAA;
const uint8_t FRAME_SYNC_2 = 0x55;

const uint8_t DISPENSE_CMD = 0x10;
const uint8_t DISPENSE_RESULT = 0x11;

const uint8_t RESULT_SUCCESS = 0x00;
const uint8_t RESULT_FAILURE = 0x01;

const uint8_t MAX_PAYLOAD_LENGTH = 32;

struct DispenseCommand
{
    uint8_t requestId[4];
    uint8_t slot;
    uint8_t quantity;
};

uint8_t crc8Maxim(const uint8_t *data, uint8_t length)
{
    uint8_t crc = 0x00;

    for (uint8_t i = 0; i < length; i++)
    {
        crc ^= data[i];

        for (uint8_t bit = 0; bit < 8; bit++)
        {
            if (crc & 0x01)
            {
                crc = (crc >> 1) ^ 0x8C;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

void sendFrame(
    uint8_t messageType,
    const uint8_t *payload,
    uint8_t payloadLength)
{
    if (payloadLength > MAX_PAYLOAD_LENGTH)
    {
        return;
    }

    uint8_t crcInput[2 + MAX_PAYLOAD_LENGTH];

    crcInput[0] = messageType;
    crcInput[1] = payloadLength;

    for (uint8_t i = 0; i < payloadLength; i++)
    {
        crcInput[2 + i] = payload[i];
    }

    uint8_t crc = crc8Maxim(crcInput, 2 + payloadLength);

    DISPENSE_SERIAL.write(FRAME_SYNC_1);
    DISPENSE_SERIAL.write(FRAME_SYNC_2);
    DISPENSE_SERIAL.write(messageType);
    DISPENSE_SERIAL.write(payloadLength);
    DISPENSE_SERIAL.write(payload, payloadLength);
    DISPENSE_SERIAL.write(crc);
}

void sendDispenseResult(
    const uint8_t requestId[4],
    uint8_t slot,
    uint8_t resultCode)
{
    // Result payload:
    // request_id[4] + slot[1] + result_code[1]
    uint8_t payload[6];

    payload[0] = requestId[0];
    payload[1] = requestId[1];
    payload[2] = requestId[2];
    payload[3] = requestId[3];
    payload[4] = slot;
    payload[5] = resultCode;

    sendFrame(DISPENSE_RESULT, payload, sizeof(payload));
}

bool performDispense(uint8_t slot, uint8_t quantity)
{
    if (quantity == 0)
    {
        return false;
    }

    // Replace this stub with the motor, compartment, and sensor control.
    (void)slot;
    return true;
}

enum ParserState
{
    WAIT_FOR_SYNC_1,
    WAIT_FOR_SYNC_2,
    READ_MESSAGE_TYPE,
    READ_LENGTH,
    READ_PAYLOAD,
    READ_CRC
};

ParserState parserState = WAIT_FOR_SYNC_1;
uint8_t receivedMessageType = 0;
uint8_t receivedPayloadLength = 0;
uint8_t receivedPayload[MAX_PAYLOAD_LENGTH];
uint8_t receivedPayloadIndex = 0;

void resetParser()
{
    parserState = WAIT_FOR_SYNC_1;
    receivedMessageType = 0;
    receivedPayloadLength = 0;
    receivedPayloadIndex = 0;
}

void handleCompleteFrame()
{
    if (receivedMessageType != DISPENSE_CMD ||
        receivedPayloadLength != 6)
    {
        return;
    }

    DispenseCommand command;

    command.requestId[0] = receivedPayload[0];
    command.requestId[1] = receivedPayload[1];
    command.requestId[2] = receivedPayload[2];
    command.requestId[3] = receivedPayload[3];
    command.slot = receivedPayload[4];
    command.quantity = receivedPayload[5];

    const bool successful = performDispense(command.slot, command.quantity);

    sendDispenseResult(
        command.requestId,
        command.slot,
        successful ? RESULT_SUCCESS : RESULT_FAILURE);
}

void processIncomingByte(uint8_t byte)
{
    switch (parserState)
    {
    case WAIT_FOR_SYNC_1:
        if (byte == FRAME_SYNC_1)
        {
            parserState = WAIT_FOR_SYNC_2;
        }
        break;

    case WAIT_FOR_SYNC_2:
        if (byte == FRAME_SYNC_2)
        {
            parserState = READ_MESSAGE_TYPE;
        }
        else if (byte != FRAME_SYNC_1)
        {
            resetParser();
        }
        break;

    case READ_MESSAGE_TYPE:
        receivedMessageType = byte;
        parserState = READ_LENGTH;
        break;

    case READ_LENGTH:
        receivedPayloadLength = byte;

        if (receivedPayloadLength > MAX_PAYLOAD_LENGTH)
        {
            resetParser();
        }
        else if (receivedPayloadLength == 0)
        {
            parserState = READ_CRC;
        }
        else
        {
            receivedPayloadIndex = 0;
            parserState = READ_PAYLOAD;
        }
        break;

    case READ_PAYLOAD:
        receivedPayload[receivedPayloadIndex++] = byte;

        if (receivedPayloadIndex >= receivedPayloadLength)
        {
            parserState = READ_CRC;
        }
        break;

    case READ_CRC:
    {
        uint8_t crcInput[2 + MAX_PAYLOAD_LENGTH];
        crcInput[0] = receivedMessageType;
        crcInput[1] = receivedPayloadLength;

        for (uint8_t i = 0; i < receivedPayloadLength; i++)
        {
            crcInput[2 + i] = receivedPayload[i];
        }

        const uint8_t calculatedCrc =
            crc8Maxim(crcInput, 2 + receivedPayloadLength);

        if (byte == calculatedCrc)
        {
            handleCompleteFrame();
        }

        resetParser();
        break;
    }
    }
}

void setup()
{
    DISPENSE_SERIAL.begin(UART_BAUD);
}

void loop()
{
    while (DISPENSE_SERIAL.available() > 0)
    {
        processIncomingByte(static_cast<uint8_t>(DISPENSE_SERIAL.read()));
    }
}