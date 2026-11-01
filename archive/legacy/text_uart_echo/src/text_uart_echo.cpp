#include <Arduino.h>

#define ECHO_SERIAL Serial

const uint32_t UART_BAUD = 9600;
const size_t MAX_MESSAGE_LENGTH = 64;

char message[MAX_MESSAGE_LENGTH];
size_t messageLength = 0;

void sendEcho()
{
    message[messageLength] = '\0';
    ECHO_SERIAL.print(message);
    ECHO_SERIAL.println(" from mcu");
    messageLength = 0;
}

void setup()
{
    ECHO_SERIAL.begin(UART_BAUD);
}

void loop()
{
    while (ECHO_SERIAL.available() > 0)
    {
        const char received = static_cast<char>(ECHO_SERIAL.read());

        if (received == '\n')
        {
            if (messageLength > 0)
            {
                sendEcho();
            }
        }

        if (received == '\r')
        {
            continue;
        }

        if (messageLength < MAX_MESSAGE_LENGTH - 1)
        {
            message[messageLength++] = received;
        }
        else
        {
            messageLength = 0;
        }
    }
}
