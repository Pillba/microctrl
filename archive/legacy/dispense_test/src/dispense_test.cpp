#include <Arduino.h>
#include <Servo.h>

Servo myservo;
const int servoPin = 9;

// Protocol constants matching the Raspberry Pi Python script
const uint8_t DISPENSE_CMD = 0x10;
const uint8_t DISPENSE_RESULT = 0x11;
const size_t HEADER_LEN = 4;
const size_t PAYLOAD_LEN = 6;
const size_t BASE_FRAME_LEN = HEADER_LEN + PAYLOAD_LEN; // 10 bytes (+ checksum if used)

// Servo angles and timing
const int REST_ANGLE = 0;
const int DISPENSE_ANGLE = 90;
const int MOVE_DELAY_MS = 400;

uint8_t rxBuffer[32];
size_t rxIndex = 0;
unsigned long lastByteTime = 0;
const unsigned long FRAME_TIMEOUT_MS = 50;

// Actuate the servo once per requested unit quantity
uint8_t dispensePills(uint8_t slot, uint8_t quantity)
{
    if (quantity == 0)
    {
        return 1; // Hardware/parameter error
    }

    for (uint8_t i = 0; i < quantity; i++)
    {
        myservo.write(DISPENSE_ANGLE);
        delay(MOVE_DELAY_MS);
        myservo.write(REST_ANGLE);
        delay(MOVE_DELAY_MS);
    }
    return 0; // 0 = success
}

// Send DISPENSE_RESULT (0x11) with payload: request_id (4B) + slot (1B) + result_code (1B)
void sendDispenseResult(const uint8_t *reqFrame, size_t totalLen, uint8_t resultCode)
{
    uint8_t txFrame[32];

    // Copy the 4-byte header from the incoming frame and swap 0x10 -> 0x11
    for (size_t i = 0; i < HEADER_LEN; i++)
    {
        txFrame[i] = (reqFrame[i] == DISPENSE_CMD) ? DISPENSE_RESULT : reqFrame[i];
    }

    // Copy request_id (bytes 4..7) and slot (byte 8)
    for (size_t i = 4; i < 9; i++)
    {
        txFrame[i] = reqFrame[i];
    }

    // Byte 9 of the frame (index 5 of payload) is result_code (0 = success, 1 = error)
    txFrame[9] = resultCode;

    // If encode_frame() appends a 1-byte checksum/CRC at byte 10, compute it here:
    if (totalLen == 11)
    {
        uint8_t checksum = 0;
        for (size_t i = 0; i < 10; i++)
        {
            checksum += txFrame[i]; // Replace with XOR or CRC-8 if schema/uart_protocol.py differs
        }
        txFrame[10] = checksum;
    }

    Serial.write(txFrame, totalLen);
    Serial.flush();
}

void processFrame(const uint8_t *frame, size_t len)
{
    if (len < BASE_FRAME_LEN)
    {
        return;
    }

    // Verify the 4-byte header contains DISPENSE_CMD (0x10)
    bool isDispenseCmd = false;
    for (size_t i = 0; i < HEADER_LEN; i++)
    {
        if (frame[i] == DISPENSE_CMD)
        {
            isDispenseCmd = true;
            break;
        }
    }
    if (!isDispenseCmd)
    {
        return;
    }

    // Extract payload fields matching frame[4:8] (request_id), frame[8] (slot), frame[9] (quantity)
    uint8_t slot = frame[8];
    uint8_t quantity = frame[9];

    // Turn the servo and send back the acknowledgement frame
    uint8_t resultCode = dispensePills(slot, quantity);
    sendDispenseResult(frame, len, resultCode);
}

void setup()
{
    Serial.begin(115200); // Match the baud rate configured in schema/uart_protocol.py
    myservo.attach(servoPin);
    myservo.write(REST_ANGLE);
}

void loop()
{
    while (Serial.available() > 0)
    {
        if (rxIndex < sizeof(rxBuffer))
        {
            rxBuffer[rxIndex++] = (uint8_t)Serial.read();
        }
        else
        {
            Serial.read(); // Discard overflow
        }
        lastByteTime = millis();
    }

    // Process the buffered frame once the UART burst finishes
    if (rxIndex > 0 && (millis() - lastByteTime >= FRAME_TIMEOUT_MS))
    {
        processFrame(rxBuffer, rxIndex);
        rxIndex = 0;
    }
}