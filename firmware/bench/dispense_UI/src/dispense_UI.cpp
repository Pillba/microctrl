// Panel-ui dispense firmware for the ATmega328P PCB.
// Framed CRC-8/MAXIM UART at 115200 baud. The automated hardware sequence is
// retained from dispense.cpp, while commands and results use the panel protocol.
// The panel sends DISPENSE_CMD with request ID, physical slot, quantity, and optionally the weight of
// one pill in mg (8-byte payload; 0 or a 6-byte payload uses the e <grams> value).
// The MCU returns DISPENSE_RESULT with the confirmed quantity.
//
// The rest of the dispense.cpp menu is available as bench frames (bench only, see protocol/protocol.md):
//   BENCH_CMD    0x80  Pi -> MCU  request_id[4] command[1] arg[4, uint32 LE]
//   BENCH_RESULT 0x81  MCU -> Pi  request_id[4] command[1] status[1] value[4, int32 LE]
//   BENCH_EVENT  0x82  MCU -> Pi  00 00 00 00   event[1]   value[4, int32 LE]
// Driven by pi/tests/integration/bench_menu_frames.py. Weights on the wire are in micrograms.
// Progress of a dispense is still logged as plain text lines between frames.
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

const uint32_t UART_BAUD = 115200;
const size_t MAX_MESSAGE_LENGTH = 64;
const uint8_t FRAME_SYNC_0 = 0xAA;
const uint8_t FRAME_SYNC_1 = 0x55;
const uint8_t DISPENSE_CMD = 0x10;
const uint8_t DISPENSE_RESULT = 0x11;
const uint8_t ERROR_RESULT = 0xFF;

// Bench frames (the menu of dispense.cpp, carried as frames)
const uint8_t BENCH_CMD = 0x80;
const uint8_t BENCH_RESULT = 0x81;
const uint8_t BENCH_EVENT = 0x82;
const uint8_t BENCH_CMD_LENGTH = 9;

// BENCH_CMD command byte       dispense.cpp menu   arg
const uint8_t CMD_STEPPER_LOOP = 0x01;   // 1 ... 6    slot
const uint8_t CMD_STOP = 0x02;           // stop
const uint8_t CMD_SHIELD = 0x03;         // 9 / 8      1 open, 0 closed
const uint8_t CMD_PUMP = 0x04;           // pon / poff 1 on, 0 off
const uint8_t CMD_TARE = 0x05;           // t
const uint8_t CMD_NOISE_TEST = 0x06;     // r          -> value raw spread
const uint8_t CMD_READ_WEIGHT = 0x07;    // w          -> value micrograms
const uint8_t CMD_CALIBRATE_START = 0x08;   // c, step 1: zero the empty scale
const uint8_t CMD_CALIBRATE_FINISH = 0x09;  // c, step 2: known weight in micrograms (0 cancels) -> factor x100
const uint8_t CMD_EXPECTED_WEIGHT = 0x0A;   // e <grams>  micrograms (0 only reads it) -> micrograms
// "test N" is the regular DISPENSE_CMD with quantity 1.

// BENCH_RESULT status byte
const uint8_t STATUS_OK = 0;
const uint8_t STATUS_FAILED = 1;          // the load cell (HX711) does not answer
const uint8_t STATUS_INVALID = 2;         // unknown command, bad argument, or calibration not started
const uint8_t STATUS_BUSY = 3;            // a dispense is running
const uint8_t STATUS_NOT_CALIBRATED = 4;  // weight read, but the scale is not calibrated on this board
const uint8_t STATUS_UNSTEADY = 5;        // calibration saved, but the readings were not steady
const uint8_t STATUS_CANCELLED = 6;       // calibration cancelled, nothing changed
const uint8_t STATUS_NO_CHANGE = 7;       // calibration: the reading did not change, nothing saved

// BENCH_EVENT event byte
const uint8_t EVENT_PLAY_SOUND = 0x01;  // value: sound number (was the text line PLAY:n)
const uint8_t EVENT_HALL = 0x02;        // value: 0 between, 1 closed, 2 open, 3 both sensors (error)
const uint8_t EVENT_READY = 0x03;       // value: bit 0 scale calibrated, bit 1 clear beam reads HIGH

const uint16_t STEPS = 1000;
const uint8_t MAX_STEP_TURNS = 3;   // a dispense steps until the IR beam counts every pill, at most MAX_STEP_TURNS * STEPS per pill
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
const float WEIGHT_TOLERANCE_PCT = 45.0;     // allowed difference, in percent of the expected weight (0.40 g target: 0.22 to 0.58 g)
const float WEIGHT_TOLERANCE_MIN_G = 0.030;  // but never less than this many grams
const float WEIGHT_COMPARE_SLACK_G = 0.0005; // float rounding: a reading exactly on the limit passes

const int ADDR_MARKER = 0;
const int ADDR_FACTOR = 1;
const byte MARKER = 0x42;
float calibrationFactor = 420.00;
bool calibrated = false;   // true when a factor was loaded from EEPROM or just measured

Servo shield;
HX711 scale;

uint8_t activeBoard = 0;
bool commandChanged = false;
bool dispenseInProgress = false;
bool calibrationStarted = false;

void pollFramedSerial();
void sendBenchEvent(uint8_t event, int32_t value);

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
  sendBenchEvent(EVENT_PLAY_SOUND, soundNumber);
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
    sendBenchEvent(EVENT_HALL, reading);
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
    pollFramedSerial();
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
    pollFramedSerial();
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
// pillWeightG is the weight of one pill for this dispense (from the DISPENSE_CMD, or expectedWeightG).
uint8_t executeAutomatedDispense(uint8_t boardNum, uint8_t quantity, float pillWeightG)
{
  ECHO_SERIAL.print(F("TEST: Starting automated dispense on board "));
  ECHO_SERIAL.print(boardNum);
  ECHO_SERIAL.print(F(", "));
  printGrams(pillWeightG);
  ECHO_SERIAL.println(F(" per pill"));
  if (!calibrated)
  {
    ECHO_SERIAL.println(F("WARNING: The scale is NOT calibrated on this board, so the weight will be wrong. Send c first."));
  }
  if (!scale.wait_ready_timeout(1000))
  {
    testFailed(F("the load cell (HX711) does not answer. Check its wiring."));
    return 0;
  }

  // 1. shield closed, confirmed by the hall sensor
  ECHO_SERIAL.println(F("TEST: Closing the shield..."));
  if (moveShield(false)) ECHO_SERIAL.println(F("VERIFY: Shield closed, hall sensor confirms."));
  else ECHO_SERIAL.println(F("WARNING: The hall sensor did NOT confirm that the shield is closed. Carrying on."));

  // 2. tare, so only what this test drops is weighed
  ECHO_SERIAL.println(F("TEST: Taring the scale..."));
  scale.tare(AVERAGE_SCANS);
  ECHO_SERIAL.println(F("VERIFY: Scale has tared."));

  // 3. stepper: until the IR beam has counted every pill, at most MAX_STEP_TURNS * STEPS per pill
  unsigned long initialBreaks = readBreaks();
  const uint32_t maxSteps = (uint32_t)STEPS * MAX_STEP_TURNS * quantity;
  ECHO_SERIAL.print(F("TEST: Running stepper "));
  ECHO_SERIAL.print(boardNum);
  ECHO_SERIAL.print(F(" until the IR beam counts "));
  ECHO_SERIAL.print(quantity);
  ECHO_SERIAL.print(F(" (at most "));
  ECHO_SERIAL.print(maxSteps);
  ECHO_SERIAL.println(F(" steps)..."));
  enableBoard(boardNum);
  delay(5);
  uint32_t stepsTaken = 0;
  while (stepsTaken < maxSteps && readBreaks() - initialBreaks < quantity)
  {
    digitalWrite(STEP_PIN, HIGH);
    delayMicroseconds(20);
    digitalWrite(STEP_PIN, LOW);
    delayMicroseconds(STEP_PERIOD_US - 20);
    if (stepsTaken % 50 == 0) pollFramedSerial();
    stepsTaken++;
  }
  disableAll();
  ECHO_SERIAL.print(F("VERIFY: Stepper stopped after "));
  ECHO_SERIAL.print(stepsTaken);
  ECHO_SERIAL.println(F(" steps."));

  // 4. IR break beam
  ECHO_SERIAL.println(F("TEST: Checking IR break beam..."));
  unsigned long waitStart = millis();
  while (
    millis() - waitStart < IR_WAIT_MS
    && readBreaks() - initialBreaks < quantity
  )
  {
    pollFramedSerial();
  }
  unsigned long settleStart = millis();
  while (millis() - settleStart < 200)
  {
    pollFramedSerial();
    delay(10);   // a second pill right behind the first would show up here
  }
  unsigned long trips = readBreaks() - initialBreaks;
  if (trips < quantity)
  {
    testFailed(F("the IR beam did not confirm every requested pill."));
    return 0;
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
    pollFramedSerial();
    if (fabs(scale.get_units(CHECK_SCANS)) >= MIN_CHANGE_G)
    {
      arrived = true;
      break;
    }
  }
  if (!arrived)
  {
    testFailed(F("the IR beam tripped but no weight reached the cup."));
    return 0;
  }
  float grams;
  bool steady = steadyReading(grams);
  ECHO_SERIAL.print(steady ? F("VERIFY: Constant reading received: ") : F("WARNING: The reading never became steady. Last value: "));
  printGrams(grams);
  ECHO_SERIAL.println();

  float expectedTotalWeightG = pillWeightG * quantity;
  float tolerance = expectedTotalWeightG * WEIGHT_TOLERANCE_PCT / 100.0;
  if (tolerance < WEIGHT_TOLERANCE_MIN_G) tolerance = WEIGHT_TOLERANCE_MIN_G;
  ECHO_SERIAL.print(F("TEST: Expected "));
  printGrams(expectedTotalWeightG);
  ECHO_SERIAL.print(F(", allowed difference "));
  printGrams(tolerance);
  ECHO_SERIAL.println();
  if (fabs(grams - expectedTotalWeightG) > tolerance + WEIGHT_COMPARE_SLACK_G)
  {
    ECHO_SERIAL.print(F("ERROR: Weight out of range. Off by "));
    printGrams(grams - expectedTotalWeightG);
    ECHO_SERIAL.println();
    testFailed(F("the weight does not match the expected weight."));
    return 0;
  }
  ECHO_SERIAL.println(F("VERIFY: Weight matches the expected weight."));

  // 6. everything checked: open the shield, then the sound
  ECHO_SERIAL.println(F("TEST: Dose fully verified! Opening the shield..."));
  if (moveShield(true)) ECHO_SERIAL.println(F("VERIFY: Shield open, hall sensor confirms."));
  else ECHO_SERIAL.println(F("WARNING: The hall sensor did NOT confirm that the shield is open."));
  sendPlay(SOUND_NUMBER);
  ECHO_SERIAL.println(F("RESULT: PASS. Send 8 to close the shield."));
  return quantity;
}

int32_t gramsToMicrograms(float grams)
{
  return (int32_t)(grams * 1000000.0 + (grams >= 0 ? 0.5 : -0.5));
}

uint32_t readUint32(const uint8_t *data)
{
  return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

void writeInt32(uint8_t *out, int32_t value)
{
  const uint32_t raw = (uint32_t)value;
  for (uint8_t i = 0; i < 4; i++) out[i] = (uint8_t)(raw >> (8 * i));
}

// Calibration, step 1 (c): the scale must be empty. Zeroes with a factor of 1 (about 10 seconds).
uint8_t startCalibration()
{
  if (!scale.wait_ready_timeout(1000)) return STATUS_FAILED;
  ECHO_SERIAL.println(F("SCALE: Zeroing, please wait about 10 seconds..."));
  scale.set_scale();   // factor of 1 so the readings are raw
  scale.tare(100);
  calibrationStarted = true;
  return STATUS_OK;
}

// Calibration, step 2: the known weight is on the scale. Measures (about 30 seconds) and saves the factor
// in EEPROM. knownUg == 0 cancels.
uint8_t finishCalibration(uint32_t knownUg, int32_t &factorX100)
{
  factorX100 = (int32_t)(calibrationFactor * 100.0);
  if (!calibrationStarted) return STATUS_INVALID;
  calibrationStarted = false;
  if (knownUg == 0)
  {
    scale.set_scale(calibrationFactor);
    return STATUS_CANCELLED;
  }

  ECHO_SERIAL.println(F("SCALE: Hold still, measuring (about 30 seconds)..."));
  const float knownGrams = knownUg / 1000000.0;
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
    scale.set_scale(calibrationFactor);
    return STATUS_NO_CHANGE;
  }
  const bool unsteady = fabs((hi - lo) / avg) * 100.0 > 1.0;
  calibrationFactor = factor;
  calibrated = true;
  scale.set_scale(calibrationFactor);
  EEPROM.update(ADDR_MARKER, MARKER);
  EEPROM.put(ADDR_FACTOR, calibrationFactor);
  factorX100 = (int32_t)(calibrationFactor * 100.0);
  return unsteady ? STATUS_UNSTEADY : STATUS_OK;
}

void sendBenchResult(const uint8_t *requestId, uint8_t command, uint8_t status, int32_t value);

// One BENCH_CMD: the same actions as the dispense.cpp menu.
void handleBenchCommand(const uint8_t *requestId, uint8_t command, uint32_t arg)
{
  uint8_t status = STATUS_OK;
  int32_t value = 0;

  if (dispenseInProgress)
  {
    sendBenchResult(requestId, command, STATUS_BUSY, 0);
    return;
  }

  switch (command)
  {
  case CMD_STEPPER_LOOP:
    if (arg >= 1 && arg <= 6)
    {
      activeBoard = arg;
      commandChanged = true;
      value = arg;
    }
    else status = STATUS_INVALID;
    break;
  case CMD_STOP:
    activeBoard = 0;
    disableAll();
    commandChanged = true;
    break;
  case CMD_SHIELD:
    shield.write(arg ? OPEN_ANGLE : CLOSED_ANGLE);
    value = arg ? 1 : 0;
    break;
  case CMD_PUMP:
    digitalWrite(PUMP_PIN, arg ? PUMP_ON_LEVEL : !PUMP_ON_LEVEL);
    value = arg ? 1 : 0;
    break;
  case CMD_TARE:
    if (scale.wait_ready_timeout(1000)) scale.tare(30);
    else status = STATUS_FAILED;
    break;
  case CMD_NOISE_TEST:
    if (scale.wait_ready_timeout(1000))
    {
      long minVal = 2147483647L;
      long maxVal = -2147483647L;
      for (int i = 0; i < 50; i++)
      {
        long v = scale.read();
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
      }
      value = maxVal - minVal;
    }
    else status = STATUS_FAILED;
    break;
  case CMD_READ_WEIGHT:
    if (scale.wait_ready_timeout(1000))
    {
      value = gramsToMicrograms(scale.get_units(AVERAGE_SCANS));
      if (!calibrated) status = STATUS_NOT_CALIBRATED;
    }
    else status = STATUS_FAILED;
    break;
  case CMD_CALIBRATE_START:
    status = startCalibration();
    break;
  case CMD_CALIBRATE_FINISH:
    status = finishCalibration(arg, value);
    break;
  case CMD_EXPECTED_WEIGHT:
    if (arg > 0) expectedWeightG = arg / 1000000.0;
    value = gramsToMicrograms(expectedWeightG);
    break;
  default:
    status = STATUS_INVALID;
    break;
  }
  sendBenchResult(requestId, command, status, value);
}

uint8_t protocolCrc(const uint8_t *data, uint8_t length)
{
  uint8_t crc = 0;
  for (uint8_t i = 0; i < length; i++)
  {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++)
      crc = (crc & 1) ? (crc >> 1) ^ 0x8C : (crc >> 1);
  }
  return crc;
}

void sendFrame(uint8_t messageType, const uint8_t *payload, uint8_t payloadLength)
{
  uint8_t header[2 + MAX_MESSAGE_LENGTH];
  header[0] = messageType;
  header[1] = payloadLength;
  for (uint8_t i = 0; i < payloadLength; i++) header[2 + i] = payload[i];
  ECHO_SERIAL.write(FRAME_SYNC_0);
  ECHO_SERIAL.write(FRAME_SYNC_1);
  ECHO_SERIAL.write(header, payloadLength + 2);
  ECHO_SERIAL.write(protocolCrc(header, payloadLength + 2));
}

void sendDispenseResult(
  const uint8_t *requestId,
  uint8_t slot,
  uint8_t resultCode,
  uint8_t confirmedQuantity
)
{
  uint8_t payload[7];
  for (uint8_t i = 0; i < 4; i++) payload[i] = requestId[i];
  payload[4] = slot;
  payload[5] = resultCode;
  payload[6] = confirmedQuantity;
  sendFrame(DISPENSE_RESULT, payload, sizeof(payload));
}

void sendBenchResult(const uint8_t *requestId, uint8_t command, uint8_t status, int32_t value)
{
  uint8_t payload[10];
  for (uint8_t i = 0; i < 4; i++) payload[i] = requestId[i];
  payload[4] = command;
  payload[5] = status;
  writeInt32(&payload[6], value);
  sendFrame(BENCH_RESULT, payload, sizeof(payload));
}

// Asynchronous event: the request ID is all zeros (protocol.md).
void sendBenchEvent(uint8_t event, int32_t value)
{
  uint8_t payload[9] = {0, 0, 0, 0, event};
  writeInt32(&payload[5], value);
  sendFrame(BENCH_EVENT, payload, sizeof(payload));
}

void handleFramedCommand(uint8_t messageType, const uint8_t *payload, uint8_t payloadLength)
{
  if (messageType == BENCH_CMD && payloadLength == BENCH_CMD_LENGTH)
  {
    handleBenchCommand(payload, payload[4], readUint32(&payload[5]));
    return;
  }
  if (messageType != DISPENSE_CMD || (payloadLength != 6 && payloadLength != 8)) return;

  const uint8_t slot = payload[4];
  const uint8_t quantity = payload[5];
  if (slot < 1 || slot > 6 || quantity == 0)
  {
    sendDispenseResult(payload, slot, 2, 0);
    return;
  }
  if (dispenseInProgress)
  {
    sendDispenseResult(payload, slot, 3, 0);
    return;
  }

  activeBoard = 0;        // like "test N" in dispense.cpp: a dispense stops the stepper loop
  commandChanged = true;
  dispenseInProgress = true;
  const uint16_t unitMg = payloadLength == 8 ? (uint16_t)payload[6] | ((uint16_t)payload[7] << 8) : 0;
  const float pillWeightG = unitMg > 0 ? unitMg / 1000.0 : expectedWeightG;
  const uint8_t confirmedQuantity = executeAutomatedDispense(slot, quantity, pillWeightG);
  const uint8_t resultCode = confirmedQuantity == quantity ? 0 : 1;
  sendDispenseResult(payload, slot, resultCode, confirmedQuantity);
  dispenseInProgress = false;
}

void pollFramedSerial()
{
  static uint8_t buffer[2 + 2 + MAX_MESSAGE_LENGTH + 1];
  static uint8_t length = 0;

  while (ECHO_SERIAL.available() > 0)
  {
    uint8_t received = static_cast<uint8_t>(ECHO_SERIAL.read());
    if (length == 0 && received != FRAME_SYNC_0) continue;
    if (length == 1 && received != FRAME_SYNC_1)
    {
      length = received == FRAME_SYNC_0 ? 1 : 0;
      continue;
    }

    buffer[length++] = received;
    if (length < 4) continue;

    uint8_t payloadLength = buffer[3];
    if (payloadLength > MAX_MESSAGE_LENGTH)
    {
      length = 0;
      continue;
    }
    uint8_t frameLength = payloadLength + 5;
    if (length < frameLength) continue;

    uint8_t expected = protocolCrc(&buffer[2], payloadLength + 2);
    length = 0;
    if (buffer[frameLength - 1] == expected)
    {
      // Copy first: a dispense polls this function again and would overwrite the buffer.
      uint8_t frame[MAX_MESSAGE_LENGTH];
      const uint8_t messageType = buffer[2];
      for (uint8_t i = 0; i < payloadLength; i++) frame[i] = buffer[4 + i];
      handleFramedCommand(messageType, frame, payloadLength);
    }
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
      pollFramedSerial();
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
    pollFramedSerial();
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
    ECHO_SERIAL.println(F("SETUP: WARNING the scale is NOT calibrated on this board. Run c before using test."));
  }
  ECHO_SERIAL.print(F("SETUP: Expected weight for test is "));
  printGrams(expectedWeightG);
  ECHO_SERIAL.println(F(". Change it with e <grams>"));
  ECHO_SERIAL.println(F("SETUP: System initialized and ready."));
  sendBenchEvent(EVENT_READY, (calibrated ? 1 : 0) | (irTrippedLevel == LOW ? 2 : 0));
}

void loop()
{
  pollFramedSerial();

  if (activeBoard != 0)
  {
    runCycle();
  }
}