#include <Arduino.h>

const int pumpPin = 7;

void setup()
{
    pinMode(pumpPin, OUTPUT);
    digitalWrite(pumpPin, LOW);

    Serial.begin(9600);

    Serial.println("--- Pump Console Control Ready ---");
    Serial.println("Type '1' and press Enter to turn ON.");
    Serial.println("Type '0' and press Enter to turn OFF.");
    Serial.println("----------------------------------");
}

void loop()
{
    if (Serial.available() > 0)
    {
        char consoleInput = Serial.read();

        switch (consoleInput)
        {
        case '1':
            digitalWrite(pumpPin, HIGH);
            Serial.println("[STATUS] Pump turned ON");
            break;

        case '0':
            digitalWrite(pumpPin, LOW);
            Serial.println("[STATUS] Pump turned OFF");
            break;

        case '\n':
        case '\r':
        case ' ':
            break;

        default:
            Serial.print("[ERROR] Invalid command received: '");
            Serial.print(consoleInput);
            Serial.println("'. Please use '1' or '0'.");
            break;
        }
    }
}