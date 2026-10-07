#include <Arduino.h>
// DRV8805 stepper test (unipolar motor driver with a STEP and DIR indexer).
// Spins one revolution forward, pauses, one revolution back, pauses, repeats.
//
// Differences from the DRV8825 that matter for wiring:
//   The DRV8805 drives UNIPOLAR motors (5 or 6 wire). The center tap of each
//   winding goes to VM, and each of the four winding ends goes to OUT1 to OUT4.
//   A normal 4 wire bipolar motor is not the right motor for this chip.
//   VM must be 8.2 V to 60 V. There is no separate logic supply.
//   nENBL is active LOW with an internal pulldown, so the outputs are on
//   unless you drive it HIGH.
//   RESET is active HIGH. Leave it low or unconnected for normal running.
//   SM1 and SM0 both low (their default) selects full step, two phase drive.
//   Share ground between the Arduino and the driver.

const uint8_t STEP_PIN = 2;
const uint8_t DIR_PIN = 3;
const uint8_t ENABLE_PIN = 4; // connects to nENBL, LOW enables the outputs

const int STEPS_PER_REV = 200;           // change if your motor is not 1.8 degrees per step
const unsigned int STEP_DELAY_US = 1500; // chip needs at least 1.9 us, motor limits real speed
const unsigned int PAUSE_MS = 1000;

void stepMotor(int steps)
{
    for (int i = 0; i < steps; i++)
    {
        digitalWrite(STEP_PIN, HIGH); // the chip steps on the rising edge
        delayMicroseconds(STEP_DELAY_US);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(STEP_DELAY_US);
    }
}

void setup()
{
    Serial.begin(9600);
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    pinMode(ENABLE_PIN, OUTPUT);
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIR_PIN, HIGH);
    digitalWrite(ENABLE_PIN, LOW); // outputs on
    Serial.println("DRV8805 test starting");
}

void loop()
{
    Serial.println("Forward");
    digitalWrite(DIR_PIN, HIGH);
    delayMicroseconds(5); // DIR must settle before the next STEP edge
    stepMotor(STEPS_PER_REV);
    delay(PAUSE_MS);

    Serial.println("Backward");
    digitalWrite(DIR_PIN, LOW);
    delayMicroseconds(5);
    stepMotor(STEPS_PER_REV);
    delay(PAUSE_MS);
}