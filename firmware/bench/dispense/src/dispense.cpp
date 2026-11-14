// Pillba bench test for the ATmega328P PCB: every subsystem by hand, plus one command that runs a whole
// dispense. Plain text over the UART at 9600 baud (no frames), for use with pillba_test_terminal.py on the Pi.
//
// Commands (the same ones as before, plus c, e and w):
//   test 1 ... test 6   whole dispense on that stepper:
//                         1. close the shield (hall sensor confirms)
//                         2. tare the scale
//                         3. run the stepper
//                         4. check that the IR beam tripped
//                         5. wait for a steady weight and compare it with the expected weight
//                         6. open the shield (hall sensor confirms) and ask the Pi to play the sound
//                       If a check fails, the shield stays closed and it says why.
//   1 ... 6             run that stepper again and again (1000 steps, pause, repeat)
//   stop                stop the stepper loop
//   9 / 8               open / close the shield
//   pon / poff          pump on / off
//   t                   tare the scale
//   r                   noise test of the scale
//   w                   one weight reading now
//   c                   calibrate the scale ON THIS BOARD: follow the messages, type the known weight in grams
//   e <grams>           set the expected weight for "test" without flashing again, for example  e 0.146
//
// IR break beam: receiver signal on pin 2, plain input (the pull up and pull down are on the PCB), no enable
// pin. The clear level is learned at power up, so keep the beam CLEAR when the board starts.

#include <Arduino.h>
#include <Servo.h>
#include "HX711.h"
#include <EEPROM.h>

#define ECHO_SERIAL Serial

#define STEP_PIN 8
#define DIR_PIN 10
#define STPR_RST 13         // DRV8805 RESET, active high
#define SERVO_PIN 9         // servo signal
#define PUMP_PIN 6          // pump 2 driver (PUMP2)
#define HALL_CLOSED_PIN 3   // hall sensor that detects the shield closed (HALL1)
#define HALL_OPEN_PIN 4     // hall sensor that detects the shield open (HALL2)
#define IR_OUT 2            // IR receiver signal wire

#define NENBL1 A0  // nENBL pins are active low
#define NENBL2 A1
#define NENBL3 A2
#define NENBL4 A3
#define NENBL5 A4
#define NENBL6 A5

// HX711 load cell pins
const int DOUT_PIN = 7;
const int SCK_PIN = 11;

const uint32_t UART_BAUD = 9600;
const size_t MAX_MESSAGE_LENGTH = 64;

const uint16_t STEPS = 1000;
const uint16_t STEP_PERIOD_US = 2000;
const uint16_t PAUSE_MS = 3000;
const uint8_t SOUND_NUMBER = 1;

const uint8_t CLOSED_ANGLE = 0;   // degrees, servo position for closed
const uint8_t OPEN_ANGLE = 90;    // degrees, servo position for open

const uint8_t PUMP_ON_LEVEL = HIGH;

const uint8_t HALL_ACTIVE_LEVEL = LOW;
const uint16_t HALL_DEBOUNCE_MS = 20;
const unsigned long SHIELD_WAIT_MS = 4000;   // how long a hall sensor gets to confirm a shield move

// IR sensor
const uint32_t MIN_GAP_US = 5000;
const unsigned long IR_WAIT_MS = 5000;       // after the stepper stops, how long a falling pill may still take
uint8_t irTrippedLevel = LOW;
volatile uint8_t isrTrippedLevel = LOW;
volatile unsigned long irBreaks = 0;
volatile uint32_t lastTripUs = 0;

// load cell
const float MG_PER_GRAM = 1000.0;
const int AVERAGE_SCANS = 20;          // scans averaged for the weight that is reported
const int CHECK_SCANS = 5;             // scans averaged for each quick check
const float MIN_CHANGE_G = 0.030;      // less than this counts as "nothing reached the cup"
const float STEADY_BAND_G = 0.020;     // the last 3 checks must agree within this to count as steady
const unsigned long WEIGHT_WAIT_MS = 5000;    // how long to wait for any weight to arrive
const unsigned long STEADY_WAIT_MS = 15000;   // how long to wait for the reading to become steady

// EXPECTED WEIGHT FOR TESTING. Change it here, or at run time with the command  e <grams>
float expectedWeightG = 0.95;                // expected weight of what one test drops, in grams
const float WEIGHT_TOLERANCE_PCT = 15.0;     // allowed difference, in percent of the expected weight
const float WEIGHT_TOLERANCE_MIN_G = 0.030;  // but never less than this many grams

const int ADDR_MARKER = 0;
const int ADDR_FACTOR = 1;
const byte MARKER = 0x42;
float calibrationFactor = 420.00;
bool calibrated = false;   // true when a factor was loaded from EEPROM or just measured

Servo shield;
HX711 scale;

char message[MAX_MESSAGE_LENGTH];
size_t messageLength = 0;

uint8_t activeBoard = 0;
bool commandChanged = false;

// hall sensor states
const uint8_t HALL_BETWEEN = 0;
const uint8_t HALL_CLOSED = 1;
const uint8_t HALL_OPEN = 2;
const uint8_t HALL_ERROR = 3;
const uint8_t HALL_UNKNOWN = 255;

uint8_t hallState = HALL_UNKNOWN;
uint8_t hallCandidate = HALL_UNKNOWN;
unsigned long hallCandidateSince = 0;

// IR interrupt handler
static inline void onBeamChange() {
  if (digitalRead(IR_OUT) == isrTrippedLevel) {
    uint32_t now = micros();
    if ((uint32_t)(now - lastTripUs) > MIN_GAP_US) {
      lastTripUs = now;
      irBreaks++;
    }
  }
}

ISR(PCINT0_vect) { onBeamChange(); }
ISR(PCINT1_vect) { onBeamChange(); }
ISR(PCINT2_vect) { onBeamChange(); }

unsigned long readBreaks()
{
  noInterrupts();
  unsigned long n = irBreaks;
  interrupts();
  return n;
}

void sendPlay(uint8_t soundNumber)
{
  ECHO_SERIAL.print(F("PLAY:"));
  ECHO_SERIAL.println(soundNumber);
}

void disableAll()
{
  digitalWrite(NENBL1, HIGH);
  digitalWrite(NENBL2, HIGH);
  digitalWrite(NENBL3, HIGH);
  digitalWrite(NENBL4, HIGH);
  digitalWrite(NENBL5, HIGH);
  digitalWrite(NENBL6, HIGH);
}

void enableBoard(uint8_t board)
{
  disableAll();
  if (board == 1) digitalWrite(NENBL1, LOW);
  if (board == 2) digitalWrite(NENBL2, LOW);
  if (board == 3) digitalWrite(NENBL3, LOW);
  if (board == 4) digitalWrite(NENBL4, LOW);
  if (board == 5) digitalWrite(NENBL5, LOW);
  if (board == 6) digitalWrite(NENBL6, LOW);
}

bool hallClosed() { return digitalRead(HALL_CLOSED_PIN) == HALL_ACTIVE_LEVEL; }
bool hallOpen() { return digitalRead(HALL_OPEN_PIN) == HALL_ACTIVE_LEVEL; }

void checkHall()
{
  uint8_t reading = HALL_BETWEEN;
  if (hallClosed() && hallOpen()) reading = HALL_ERROR;
  else if (hallClosed()) reading = HALL_CLOSED;
  else if (hallOpen()) reading = HALL_OPEN;

  if (reading != hallCandidate)
  {
    hallCandidate = reading;
    hallCandidateSince = millis();
    return;
  }

  if (reading != hallState && millis() - hallCandidateSince >= HALL_DEBOUNCE_MS)
  {
    hallState = reading;
    if (reading == HALL_CLOSED) ECHO_SERIAL.println(F("HALL:closed"));
    else if (reading == HALL_OPEN) ECHO_SERIAL.println(F("HALL:open"));
    else if (reading == HALL_ERROR) ECHO_SERIAL.println(F("HALL:error, both sensors detect"));
  }
}

// Moves the shield and waits for the matching hall sensor. Returns true if it confirmed the move.
bool moveShield(bool open)
{
  shield.write(open ? OPEN_ANGLE : CLOSED_ANGLE);
  ECHO_SERIAL.println(open ? F("SERVO:cmd open") : F("SERVO:cmd closed"));
  unsigned long start = millis();
  while (millis() - start < SHIELD_WAIT_MS)
  {
    checkHall();
    if (open ? hallOpen() : hallClosed())
    {
      delay(300);   // let the servo finish its travel
      checkHall();
      return true;
    }
  }
  return false;
}

void printGrams(float grams)
{
  ECHO_SERIAL.print(grams, 3);
  ECHO_SERIAL.print(F(" g ("));
  ECHO_SERIAL.print(grams * MG_PER_GRAM, 0);
  ECHO_SERIAL.print(F(" mg)"));
}

// Waits until 3 quick checks agree, then measures properly. Returns true if the reading became steady.
bool steadyReading(float &grams)
{
  float recent[3] = {0, 0, 0};
  uint8_t filled = 0;
  bool steady = false;
  unsigned long start = millis();
  while (millis() - start < STEADY_WAIT_MS)
  {
    recent[0] = recent[1];
    recent[1] = recent[2];
    recent[2] = scale.get_units(CHECK_SCANS);
    if (filled < 3) filled++;
    float lo = min(recent[0], min(recent[1], recent[2]));
    float hi = max(recent[0], max(recent[1], recent[2]));
    if (filled >= 3 && hi - lo <= STEADY_BAND_G)
    {
      steady = true;
      break;
    }
  }
  grams = scale.get_units(AVERAGE_SCANS);
  return steady;
}

void testFailed(const __FlashStringHelper *reason)
{
  ECHO_SERIAL.print(F("RESULT: FAIL, "));
  ECHO_SERIAL.println(reason);
  ECHO_SERIAL.println(F("TEST: The shield stays closed."));
}

// Whole dispense on one stepper: shield, tare, stepper, IR, weight, shield open, sound.
void executeAutomatedDispense(uint8_t boardNum)
{
  ECHO_SERIAL.print(F("TEST: Starting automated dispense on board "));
  ECHO_SERIAL.println(boardNum);
  if (!calibrated)
  {
    ECHO_SERIAL.println(F("WARNING: The scale is NOT calibrated on this board, so the weight will be wrong. Send c first."));
  }
  if (!scale.wait_ready_timeout(1000))
  {
    testFailed(F("the load cell (HX711) does not answer. Check its wiring."));
    return;
  }

  // 1. shield closed, confirmed by the hall sensor
  ECHO_SERIAL.println(F("TEST: Closing the shield..."));
  if (moveShield(false)) ECHO_SERIAL.println(F("VERIFY: Shield closed, hall sensor confirms."));
  else ECHO_SERIAL.println(F("WARNING: The hall sensor did NOT confirm that the shield is closed. Carrying on."));

  // 2. tare, so only what this test drops is weighed
  ECHO_SERIAL.println(F("TEST: Taring the scale..."));
  scale.tare(AVERAGE_SCANS);
  ECHO_SERIAL.println(F("VERIFY: Scale has tared."));

  // 3. stepper
  unsigned long initialBreaks = readBreaks();
  ECHO_SERIAL.print(F("TEST: Running stepper "));
  ECHO_SERIAL.print(boardNum);
  ECHO_SERIAL.print(F(" for "));
  ECHO_SERIAL.print(STEPS);
  ECHO_SERIAL.println(F(" steps..."));
  enableBoard(boardNum);
  delay(5);
  for (uint16_t s = 0; s < STEPS; s++)
  {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(20);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(STEP_PERIOD_US - 20);
  }
  disableAll();
  ECHO_SERIAL.println(F("VERIFY: Stepper finished."));

  // 4. IR break beam
  ECHO_SERIAL.println(F("TEST: Checking IR break beam..."));
  unsigned long waitStart = millis();
  while (millis() - waitStart < IR_WAIT_MS && readBreaks() == initialBreaks) { }
  delay(200);   // a second pill right behind the first would show up here
  unsigned long trips = readBreaks() - initialBreaks;
  if (trips == 0)
  {
    testFailed(F("the IR beam was not tripped. Nothing fell, or the beam did not see it."));
    return;
  }
  ECHO_SERIAL.print(F("VERIFY: IR beam tripped "));
  ECHO_SERIAL.print(trips);
  ECHO_SERIAL.println(trips == 1 ? F(" time.") : F(" times. More than one thing may have fallen."));

  // 5. load cell: wait for weight to arrive, then for a steady reading, then compare
  ECHO_SERIAL.println(F("TEST: Waiting for the weight to arrive and become steady..."));
  bool arrived = false;
  waitStart = millis();
  while (millis() - waitStart < WEIGHT_WAIT_MS)
  {
    if (fabs(scale.get_units(CHECK_SCANS)) >= MIN_CHANGE_G)
    {
      arrived = true;
      break;
    }
  }
  if (!arrived)
  {
    testFailed(F("the IR beam tripped but no weight reached the cup."));
    return;
  }
  float grams;
  bool steady = steadyReading(grams);
  ECHO_SERIAL.print(steady ? F("VERIFY: Constant reading received: ") : F("WARNING: The reading never became steady. Last value: "));
  printGrams(grams);
  ECHO_SERIAL.println();

  float tolerance = expectedWeightG * WEIGHT_TOLERANCE_PCT / 100.0;
  if (tolerance < WEIGHT_TOLERANCE_MIN_G) tolerance = WEIGHT_TOLERANCE_MIN_G;
  ECHO_SERIAL.print(F("TEST: Expected "));
  printGrams(expectedWeightG);
  ECHO_SERIAL.print(F(", allowed difference "));
  printGrams(tolerance);
  ECHO_SERIAL.println();
  if (fabs(grams - expectedWeightG) > tolerance)
  {
    ECHO_SERIAL.print(F("ERROR: Weight out of range. Off by "));
    printGrams(grams - expectedWeightG);
    ECHO_SERIAL.println();
    testFailed(F("the weight does not match the expected weight."));
    return;
  }
  ECHO_SERIAL.println(F("VERIFY: Weight matches the expected weight."));

  // 6. everything checked: open the shield, then the sound
  ECHO_SERIAL.println(F("TEST: Dose fully verified! Opening the shield..."));
  if (moveShield(true)) ECHO_SERIAL.println(F("VERIFY: Shield open, hall sensor confirms."));
  else ECHO_SERIAL.println(F("WARNING: The hall sensor did NOT confirm that the shield is open."));
  sendPlay(SOUND_NUMBER);
  ECHO_SERIAL.println(F("RESULT: PASS. Send 8 to close the shield."));
}

// Waits for one line from the Pi (used by the calibration). Returns false after the timeout.
bool waitForLine(unsigned long timeoutMs)
{
  messageLength = 0;
  unsigned long start = millis();
  while (millis() - start < timeoutMs)
  {
    while (ECHO_SERIAL.available() > 0)
    {
      const char received = static_cast<char>(ECHO_SERIAL.read());
      if (received == '\n' || received == '\r')
      {
        if (messageLength > 0)
        {
          message[messageLength] = '\0';
          messageLength = 0;
          return true;
        }
        continue;
      }
      if (messageLength < MAX_MESSAGE_LENGTH - 1) message[messageLength++] = received;
    }
    delay(10);
  }
  messageLength = 0;
  return false;
}

// Calibrates the scale on this board with any known weight and saves the factor in EEPROM.
void calibrateScale()
{
  if (!scale.wait_ready_timeout(1000))
  {
    ECHO_SERIAL.println(F("ERROR: The load cell (HX711) does not answer. Check its wiring."));
    return;
  }
  ECHO_SERIAL.println(F("SCALE: Calibration. Remove everything from the scale (the empty cup may stay)."));
  delay(4000);
  scale.set_scale();   // factor of 1 so the readings are raw
  ECHO_SERIAL.println(F("SCALE: Zeroing, please wait about 10 seconds..."));
  scale.tare(100);

  ECHO_SERIAL.println(F("SCALE: Place the known weight in the CENTER of the scale."));
  ECHO_SERIAL.println(F("SCALE: Then type its weight in GRAMS and press Enter (example: 10)."));
  while (ECHO_SERIAL.available() > 0) ECHO_SERIAL.read();
  float knownGrams = waitForLine(120000UL) ? atof(message) : 0;
  if (knownGrams <= 0)
  {
    ECHO_SERIAL.println(F("SCALE: No valid weight received. Calibration cancelled, nothing changed."));
    scale.set_scale(calibrationFactor);
    return;
  }

  ECHO_SERIAL.println(F("SCALE: Hold still, measuring (about 30 seconds)..."));
  delay(4000);
  float m[3];
  for (int i = 0; i < 3; i++)
  {
    m[i] = scale.get_value(100);
    delay(500);
  }
  float avg = (m[0] + m[1] + m[2]) / 3.0;
  float lo = min(m[0], min(m[1], m[2]));
  float hi = max(m[0], max(m[1], m[2]));
  float factor = avg / knownGrams;
  if (fabs(factor) < 0.5)
  {
    ECHO_SERIAL.println(F("SCALE: The reading did not change. Is the weight on the scale? Nothing saved."));
    scale.set_scale(calibrationFactor);
    return;
  }
  if (fabs((hi - lo) / avg) * 100.0 > 1.0)
  {
    ECHO_SERIAL.println(F("SCALE: Warning, the readings were not steady. Check the mounting and calibrate again."));
  }
  calibrationFactor = factor;
  calibrated = true;
  scale.set_scale(calibrationFactor);
  EEPROM.update(ADDR_MARKER, MARKER);
  EEPROM.put(ADDR_FACTOR, calibrationFactor);
  ECHO_SERIAL.print(F("SCALE: New calibration factor (saved on this board): "));
  ECHO_SERIAL.println(calibrationFactor, 2);
  ECHO_SERIAL.println(F("SCALE: Send w to read the weight now, it should show the known weight. Then remove it and send t."));
}

void handleMessage()
{
  message[messageLength] = '\0';
  messageLength = 0;

  if (strcmp_P(message, PSTR("stop")) == 0)
  {
    activeBoard = 0;
    disableAll();
    commandChanged = true;
    ECHO_SERIAL.println(F("STOP:ok"));
  }
  else if (strncmp_P(message, PSTR("test "), 5) == 0)
  {
    uint8_t boardNum = message[5] - '0';
    if (boardNum >= 1 && boardNum <= 6 && message[6] == '\0')
    {
      activeBoard = 0;
      commandChanged = true;
      executeAutomatedDispense(boardNum);
    }
    else
    {
      ECHO_SERIAL.println(F("ERROR: Invalid test board number (use test 1 to test 6)"));
    }
  }
  else if (message[1] == '\0' && message[0] >= '1' && message[0] <= '6')
  {
    activeBoard = message[0] - '0';
    commandChanged = true;
    ECHO_SERIAL.print(F("STEP:"));
    ECHO_SERIAL.print(activeBoard);
    ECHO_SERIAL.println(F(" started"));
  }
  else if (strcmp_P(message, PSTR("9")) == 0)
  {
    shield.write(OPEN_ANGLE);
    ECHO_SERIAL.println(F("SERVO:cmd open"));
  }
  else if (strcmp_P(message, PSTR("8")) == 0)
  {
    shield.write(CLOSED_ANGLE);
    ECHO_SERIAL.println(F("SERVO:cmd closed"));
  }
  else if (strcmp_P(message, PSTR("pon")) == 0)
  {
    digitalWrite(PUMP_PIN, PUMP_ON_LEVEL);
    ECHO_SERIAL.println(F("PUMP:on"));
  }
  else if (strcmp_P(message, PSTR("poff")) == 0)
  {
    digitalWrite(PUMP_PIN, !PUMP_ON_LEVEL);
    ECHO_SERIAL.println(F("PUMP:off"));
  }
  else if (strcmp_P(message, PSTR("t")) == 0)
  {
    ECHO_SERIAL.println(F("SCALE: Taring scale..."));
    scale.tare(30);
    ECHO_SERIAL.println(F("SCALE: Tare complete."));
  }
  else if (strcmp_P(message, PSTR("w")) == 0)
  {
    ECHO_SERIAL.print(F("SCALE: Weight "));
    printGrams(scale.get_units(AVERAGE_SCANS));
    ECHO_SERIAL.println(calibrated ? F("") : F("  (NOT calibrated, send c)"));
  }
  else if (strcmp_P(message, PSTR("c")) == 0)
  {
    calibrateScale();
  }
  else if (message[0] == 'e' && (message[1] == ' ' || message[1] == '\0'))
  {
    float grams = (message[1] == ' ') ? atof(message + 2) : 0;
    if (grams > 0) expectedWeightG = grams;
    ECHO_SERIAL.print(F("TEST: Expected weight is "));
    printGrams(expectedWeightG);
    ECHO_SERIAL.println(grams > 0 ? F(" (changed, until the next power up)") : F(". To change it send  e <grams>"));
  }
  else if (strcmp_P(message, PSTR("r")) == 0)
  {
    ECHO_SERIAL.println(F("SCALE: Running noise test..."));
    long minVal = 2147483647L;
    long maxVal = -2147483647L;
    for (int i = 0; i < 50; i++)
    {
      long v = scale.read();
      if (v < minVal) minVal = v;
      if (v > maxVal) maxVal = v;
    }
    ECHO_SERIAL.print(F("SCALE: Raw spread over 50 readings: "));
    ECHO_SERIAL.println(maxVal - minVal);
  }
  else
  {
    ECHO_SERIAL.print(message);
    ECHO_SERIAL.println(F(" from mcu"));
  }
}

void pollSerial()
{
  while (ECHO_SERIAL.available() > 0)
  {
    const char received = static_cast<char>(ECHO_SERIAL.read());
    if (received == '\n' || received == '\r')
    {
      if (messageLength > 0) handleMessage();
      continue;
    }
    if (messageLength < MAX_MESSAGE_LENGTH - 1) message[messageLength++] = received;
    else messageLength = 0;
  }
  checkHall();
}

void runCycle()
{
  commandChanged = false;
  enableBoard(activeBoard);
  delay(5);

  for (uint16_t s = 0; s < STEPS; s++)
  {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(20);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(STEP_PERIOD_US - 20);

    if (s % 50 == 0)
    {
      pollSerial();
      if (commandChanged)
      {
        disableAll();
        return;
      }
    }
  }

  disableAll();
  sendPlay(SOUND_NUMBER);

  unsigned long start = millis();
  while (millis() - start < PAUSE_MS)
  {
    pollSerial();
    if (commandChanged) return;
  }
}

void setup()
{
  ECHO_SERIAL.begin(UART_BAUD);

  digitalWrite(PUMP_PIN, !PUMP_ON_LEVEL);
  pinMode(PUMP_PIN, OUTPUT);

  pinMode(HALL_CLOSED_PIN, INPUT_PULLUP);
  pinMode(HALL_OPEN_PIN, INPUT_PULLUP);
  pinMode(IR_OUT, INPUT);   // the pull up and pull down are on the PCB

  digitalWrite(NENBL1, HIGH);
  digitalWrite(NENBL2, HIGH);
  digitalWrite(NENBL3, HIGH);
  digitalWrite(NENBL4, HIGH);
  digitalWrite(NENBL5, HIGH);
  digitalWrite(NENBL6, HIGH);
  pinMode(NENBL1, OUTPUT);
  pinMode(NENBL2, OUTPUT);
  pinMode(NENBL3, OUTPUT);
  pinMode(NENBL4, OUTPUT);
  pinMode(NENBL5, OUTPUT);
  pinMode(NENBL6, OUTPUT);

  digitalWrite(STEP_PIN, LOW);
  pinMode(STEP_PIN, OUTPUT);
  digitalWrite(DIR_PIN, HIGH);
  pinMode(DIR_PIN, OUTPUT);

  digitalWrite(STPR_RST, HIGH);
  pinMode(STPR_RST, OUTPUT);
  delay(2);
  digitalWrite(STPR_RST, LOW);

  shield.attach(SERVO_PIN);
  shield.write(CLOSED_ANGLE);

  // load cell: use the factor saved on THIS board, if there is one
  scale.begin(DOUT_PIN, SCK_PIN);
  if (EEPROM.read(ADDR_MARKER) == MARKER)
  {
    EEPROM.get(ADDR_FACTOR, calibrationFactor);
    calibrated = (fabs(calibrationFactor) >= 0.5 && fabs(calibrationFactor) < 1000000.0);
    if (!calibrated) calibrationFactor = 420.00;
  }
  scale.set_scale(calibrationFactor);
  if (scale.wait_ready_timeout(1000)) scale.tare(30);
  else ECHO_SERIAL.println(F("ERROR: The load cell (HX711) does not answer. Check its wiring."));

  // learn the IR clear level
  unsigned int highs = 0, lows = 0;
  unsigned long start = millis();
  while (millis() - start < 500)
  {
    if (digitalRead(IR_OUT) == HIGH) highs++;
    else lows++;
    delayMicroseconds(200);
  }
  irTrippedLevel = (highs >= lows) ? LOW : HIGH;

  noInterrupts();
  isrTrippedLevel = irTrippedLevel;
  irBreaks = 0;
  interrupts();
  *digitalPinToPCMSK(IR_OUT) |= bit(digitalPinToPCMSKbit(IR_OUT));
  *digitalPinToPCICR(IR_OUT) |= bit(digitalPinToPCICRbit(IR_OUT));

  ECHO_SERIAL.print(F("SETUP: IR beam, a clear beam reads "));
  ECHO_SERIAL.println(irTrippedLevel == LOW ? F("HIGH.") : F("LOW."));
  if (calibrated)
  {
    ECHO_SERIAL.print(F("SETUP: Scale factor loaded from this board: "));
    ECHO_SERIAL.println(calibrationFactor, 2);
  }
  else
  {
    ECHO_SERIAL.println(F("SETUP: WARNING the scale is NOT calibrated on this board. Send c before using test."));
  }
  ECHO_SERIAL.print(F("SETUP: Expected weight for test is "));
  printGrams(expectedWeightG);
  ECHO_SERIAL.println(F(". Change it with  e <grams>"));
  ECHO_SERIAL.println(F("SETUP: System initialized and ready."));
}

void loop()
{
  pollSerial();

  if (activeBoard != 0)
  {
    runCycle();
  }
}