// Stepper enable line test for the 6 DRV8805 boards. No Pi needed, you type in the serial monitor (115200).
//
//   0      runs a full cycle: board 1, then 2, then 3 and so on up to 6, 2 seconds each
//   1 to 6 runs only that board for 2 seconds
//
// Only the running board's nENBL goes low. Every other board stays disabled, so exactly one motor
// should turn at a time. The monitor prints which board is running.

#include <Arduino.h>

#define STEP_PIN 8
#define DIR_PIN 10
#define STPR_RST 13  // DRV8805 RESET, active high

static const uint8_t NUM_STEPPERS = 6;
static const uint8_t NENBL[NUM_STEPPERS] = {A0, A1, A2, A3, A4, A5};  // active low

static const uint16_t STEPS = 1000;  // 1000 steps x 2 ms = 2 seconds
static const uint16_t STEP_PERIOD_US = 2000;
static const uint16_t PAUSE_MS = 1000;  // gap between boards in a full cycle

static void disableAll()
{
    for (uint8_t i = 0; i < NUM_STEPPERS; i++)
    {
        digitalWrite(NENBL[i], HIGH);
    }
}

// Runs one board (0 based index) for about 2 seconds.
static void runBoard(uint8_t board)
{
    Serial.print("Board ");
    Serial.print(board + 1);
    Serial.println(" running");

    disableAll();
    digitalWrite(NENBL[board], LOW);  // enable only this board
    delay(5);
    for (uint16_t s = 0; s < STEPS; s++)
    {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(20);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(STEP_PERIOD_US - 20);
    }
    disableAll();

    Serial.print("Board ");
    Serial.print(board + 1);
    Serial.println(" done");
}

static void runCycle()
{
    Serial.println("Full cycle, boards 1 to 6, 2 seconds each");
    for (uint8_t b = 0; b < NUM_STEPPERS; b++)
    {
        runBoard(b);
        if (b + 1 < NUM_STEPPERS)
        {
            delay(PAUSE_MS);
        }
    }
    Serial.println("Full cycle done");
}

static void printMenu()
{
    Serial.println();
    Serial.println("Type a number and press Enter:");
    Serial.println("  0     full cycle, every board one by one");
    Serial.print("  1 to ");
    Serial.print(NUM_STEPPERS);
    Serial.println("  that board only");
}

void setup()
{
    Serial.begin(115200);

    for (uint8_t i = 0; i < NUM_STEPPERS; i++)
    {
        digitalWrite(NENBL[i], HIGH);
        pinMode(NENBL[i], OUTPUT);
    }
    digitalWrite(STEP_PIN, LOW);
    pinMode(STEP_PIN, OUTPUT);
    digitalWrite(DIR_PIN, HIGH);
    pinMode(DIR_PIN, OUTPUT);

    digitalWrite(STPR_RST, HIGH);  // pulse RESET once so the drivers start clean
    pinMode(STPR_RST, OUTPUT);
    delay(2);
    digitalWrite(STPR_RST, LOW);

    Serial.println("Stepper enable test. Only one motor should turn at a time.");
    printMenu();
}

void loop()
{
    if (!Serial.available())
    {
        return;
    }

    const char c = (char)Serial.read();

    if (c == '\n' || c == '\r' || c == ' ')
    {
        return;  // the monitor adds a line ending
    }

    if (c == '0')
    {
        runCycle();
    }
    else if (c >= '1' && c <= (char)('0' + NUM_STEPPERS))
    {
        runBoard((uint8_t)(c - '1'));
    }
    else
    {
        Serial.print("Unknown key '");
        Serial.print(c);
        Serial.println("'");
    }

    while (Serial.available())
    {
        Serial.read();  // drop anything typed while a motor was running
    }
    printMenu();
}
