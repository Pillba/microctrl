/*
  Load cell with HX711 board for Arduino IDE

  Library needed: "HX711" by Bogdan Necula (bogde)

  Wiring (typical Arduino Uno):
    HX711 VCC  to Arduino 5V
    HX711 GND  to Arduino GND
    HX711 DT   to Arduino pin 5
    HX711 SCK  to Arduino pin 6

  Serial Monitor: set baud to 9600 and line ending to "Newline".

  Serial commands:
    t  tare (empty the scale first)
    c  calibrate with a known weight (type the weight in GRAMS, example: 10)
    r  noise test (shows how much the raw reading wanders)
*/

#include "HX711.h"
#include <EEPROM.h>

const int DOUT_PIN = 5;
const int SCK_PIN = 6;

const float MG_PER_GRAM = 1000.0;

// Zero stabilizing settings
const float ZERO_BAND = 0.010;       // grams: readings this close to zero show as exactly 0
const float AUTOZERO_LIMIT = 0.050;  // grams: only auto correct when this close to zero
const float AUTOZERO_RATE = 0.05;    // fraction of the error removed on each update

// EEPROM layout: one marker byte, then the saved float factor
const int ADDR_MARKER = 0;
const int ADDR_FACTOR = 1;
const byte MARKER = 0x42;

HX711 scale;

// Used only if nothing has been saved yet
float calibrationFactor = 420.00;

unsigned long lastPrint = 0;

void setup() {
  Serial.begin(9600);
  scale.begin(DOUT_PIN, SCK_PIN);

  Serial.println("HX711 load cell starting");

  // Load the saved calibration factor if one exists
  if (EEPROM.read(ADDR_MARKER) == MARKER) {
    EEPROM.get(ADDR_FACTOR, calibrationFactor);
    Serial.print("Loaded saved calibration factor: ");
    Serial.println(calibrationFactor, 2);
  } else {
    Serial.println("No saved calibration yet, using default. Send c to calibrate.");
  }

  Serial.println("Keep the scale empty while it zeroes...");
  scale.set_scale(calibrationFactor);
  delay(1000);        // let the electronics settle
  scale.tare(30);     // zero using the average of 30 readings

  Serial.println("Ready.");
  Serial.println("Send t to tare, c to calibrate, r for a noise test.");
}

void loop() {
  if (millis() - lastPrint >= 500) {
    lastPrint = millis();

    // Wait up to 1 second for the chip instead of checking once
    if (scale.wait_ready_timeout(1000)) {
      float grams = scale.get_units(10);

      // Slowly pull the zero point toward the true empty reading
      if (fabs(grams) < AUTOZERO_LIMIT) {
        long newOffset = scale.get_offset() + (long)(grams * calibrationFactor * AUTOZERO_RATE);
        scale.set_offset(newOffset);
      }

      // Show exactly zero when the reading is within the deadband
      if (fabs(grams) < ZERO_BAND) grams = 0.0;

      float milligrams = grams * MG_PER_GRAM;

      Serial.print("Weight: ");
      Serial.print(grams, 3);
      Serial.print(" g | ");
      Serial.print(milligrams, 0);
      Serial.println(" mg");
    } else {
      Serial.println("HX711 not found. Check wiring.");
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 't') {
      Serial.println("Taring, keep the scale empty...");
      scale.tare(30);
      Serial.println("Tare done.");
    } else if (cmd == 'c') {
      calibrate();
    } else if (cmd == 'r') {
      noiseTest();
    }
  }
}

void noiseTest() {
  Serial.println("Noise test: leave the scale untouched for a few seconds...");
  long minVal = 2147483647L;
  long maxVal = -2147483647L;
  for (int i = 0; i < 50; i++) {
    long v = scale.read();
    if (v < minVal) minVal = v;
    if (v > maxVal) maxVal = v;
  }
  Serial.print("Raw spread over 50 readings: ");
  Serial.println(maxVal - minVal);
  Serial.print("That is about ");
  Serial.print((maxVal - minVal) / calibrationFactor, 3);
  Serial.println(" g of wobble at your current factor.");
}

void calibrate() {
  // Step 1: zero with nothing on the scale
  Serial.println("Calibration: remove everything from the scale.");
  delay(4000);
  scale.set_scale();   // factor of 1 so we read raw values
  Serial.println("Zeroing, please wait about 10 seconds...");
  scale.tare(100);     // long average for a clean zero

  // Step 2: place the known weight, then type its weight in grams
  Serial.println("Place the known weight in the CENTER of the scale.");
  Serial.println("Then type its weight in GRAMS and send it (example: 10).");

  while (Serial.available()) Serial.read();
  while (!Serial.available()) {
    delay(10);
  }
  float knownGrams = Serial.parseFloat();
  delay(100);
  while (Serial.available()) Serial.read();   // clear the leftover newline

  if (knownGrams <= 0) {
    Serial.println("That weight was not valid. Calibration cancelled.");
    scale.set_scale(calibrationFactor);
    return;
  }

  // Step 3: wait for the weight to settle, then measure three separate times
  Serial.println("Hold still, measuring (about 30 seconds)...");
  delay(4000);

  float m[3];
  for (int i = 0; i < 3; i++) {
    m[i] = scale.get_value(100);   // raw average minus tare offset
    delay(500);
  }

  float avg = (m[0] + m[1] + m[2]) / 3.0;
  float lo = min(m[0], min(m[1], m[2]));
  float hi = max(m[0], max(m[1], m[2]));
  float spreadPercent = (hi - lo) / avg * 100.0;

  Serial.print("Spread between the 3 measurements: ");
  Serial.print(spreadPercent, 2);
  Serial.println(" percent");

  if (spreadPercent > 1.0) {
    Serial.println("Warning: readings are not stable. Check mounting and wiring, then try again.");
  }

  calibrationFactor = avg / knownGrams;
  scale.set_scale(calibrationFactor);

  // Save to EEPROM so it survives a restart
  EEPROM.update(ADDR_MARKER, MARKER);
  EEPROM.put(ADDR_FACTOR, calibrationFactor);

  Serial.print("New calibration factor (saved): ");
  Serial.println(calibrationFactor, 2);
  Serial.println("Remove the weight and send t to tare again.");
}