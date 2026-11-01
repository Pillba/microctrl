// Pillba - stepper board test with the MCU board (ATmega328P @ 16 MHz)
// Tests one stepper board at a time: spins both directions, then checks nENBL.
//
// Pin map (from the MCU schematic):
//   STEP     = PB0 (chip pin 12) = Arduino D8
//   DIR      = PB2 (chip pin 14) = Arduino D10
//   STPR_RST = PB5 (chip pin 17) = Arduino D13  -> driven HIGH so it can feed the stepper board's 5V pin
//   nENBL1-6 = PC0-PC5 (chip pins 23-28) = Arduino A0-A5
//
// STEP and DIR go to all 6 boards. nENBL picks which board listens:
// LOW = that board drives its motor, HIGH = that board's outputs are off.

const int STEP_PIN = 8;
const int DIR_PIN  = 10;
const int RST_PIN  = 13;
const int EN_PINS[6] = {A0, A1, A2, A3, A4, A5};

const int TEST_BOARD    = 1;    // 0 = StepperBoard1, 1 = StepperBoard2, ... 5 = StepperBoard6
const int STEPS         = 200;
const int STEP_DELAY_MS = 5;    // if the motor buzzes but won't turn, raise this to 10-20

void setup() {
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);

  pinMode(RST_PIN, OUTPUT);
  digitalWrite(RST_PIN, HIGH);   // acts as a weak 5V source for the stepper board's 5V pin

  for (int i = 0; i < 6; i++) {
    pinMode(EN_PINS[i], OUTPUT);
    digitalWrite(EN_PINS[i], HIGH);  // start with every board disabled
  }
}

void loop() {
  // Part 1: board ENABLED -> motor should turn one way, then back
  digitalWrite(EN_PINS[TEST_BOARD], LOW);
  digitalWrite(DIR_PIN, HIGH);
  stepMotor(STEPS);
  delay(1000);
  digitalWrite(DIR_PIN, LOW);
  stepMotor(STEPS);
  delay(1000);

  // Part 2: board DISABLED -> STEP pulses still sent, motor should NOT move (goes limp)
  digitalWrite(EN_PINS[TEST_BOARD], HIGH);
  stepMotor(STEPS);
  delay(2000);
}

void stepMotor(int n) {
  for (int i = 0; i < n; i++) {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(STEP_PIN, LOW);
    delay(STEP_DELAY_MS);
  }
}