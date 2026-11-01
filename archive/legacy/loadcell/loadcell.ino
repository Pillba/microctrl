/*
  Load cell with HX711 board for Arduino IDE

  Library needed: "HX711" by Bogdan Necula (bogde)
  Install it in Arduino IDE: Sketch > Include Library > Manage Libraries, search HX711.

  Wiring (typical Arduino Uno):
    HX711 VCC  to Arduino 5V
    HX711 GND  to Arduino GND
    HX711 DT   to Arduino pin 5
    HX711 SCK  to Arduino pin 6

  Load cell wires to the HX711 board (colors vary by load cell, check your datasheet):
    Red   to E+
    Black to E minus
    White to A minus
    Green to A+

  Serial Monitor: set baud to 9600 and line ending to "Newline".

  Serial commands:
    t  tare (set the current weight as zero, remove everything from the scale first)
    c  calibrate with a known weight (type the known weight in GRAMS)
*/

#include "HX711.h"

const int DOUT_PIN = 5;
const int SCK_PIN = 6;

// 1 gram = 1000 milligrams
const float MG_PER_GRAM = 1000.0;

HX711 scale;

// This number converts raw readings into grams.
// It is replaced by your own value once you run calibration with the c command.
float calibrationFactor = 420.00;

unsigned long lastPrint = 0;

void setup() {
  Serial.begin(9600);
  scale.begin(DOUT_PIN, SCK_PIN);

  Serial.println("HX711 load cell starting");
  Serial.println("Keep the scale empty while it zeroes...");

  scale.set_scale(calibrationFactor);
  scale.tare();  // zero the scale using the average of several readings

  Serial.println("Ready.");
  Serial.println("Send t to tare, c to calibrate.");
}

void loop() {
  // Print the weight twice per second
  if (millis() - lastPrint >= 500) {
    lastPrint = millis();
    if (scale.is_ready()) {
      float grams = scale.get_units(10);          // average of 10 readings to reduce noise
      float milligrams = grams * MG_PER_GRAM;     // convert grams to milligrams

      Serial.print("Weight: ");
      Serial.print(grams, 3);                     // grams with 3 decimals
      Serial.print(" g | ");
      Serial.print(milligrams, 0);                // milligrams with no decimals
      Serial.println(" mg");
    } else {
      Serial.println("HX711 not found. Check wiring.");
    }
  }

  if (Serial.available()) {
    char cmd = Serial.read();
    if (cmd == 't') {
      Serial.println("Taring, keep the scale empty...");
      scale.tare();
      Serial.println("Tare done.");
    } else if (cmd == 'c') {
      calibrate();
    }
  }
}

void calibrate() {
  // Step 1: zero with nothing on the scale
  Serial.println("Calibration: remove everything from the scale.");
  delay(3000);
  scale.set_scale();  // reset the factor to 1 so we read raw values
  scale.tare();

  // Step 2: place something whose weight you know
  Serial.println("Place a known weight on the scale now.");
  Serial.println("Then type its weight in GRAMS and send it (example: 500).");

  while (Serial.available()) Serial.read();  // clear leftover characters
  while (!Serial.available()) {
    delay(10);
  }
  float knownGrams = Serial.parseFloat();

  if (knownGrams <= 0) {
    Serial.println("That weight was not valid. Calibration cancelled.");
    scale.set_scale(calibrationFactor);
    return;
  }

  // Step 3: raw reading divided by the real weight gives the calibration factor
  delay(1000);
  float raw = scale.get_value(20);  // average of 20 raw readings minus the tare offset
  calibrationFactor = raw / knownGrams;
  scale.set_scale(calibrationFactor);

  Serial.print("New calibration factor: ");
  Serial.println(calibrationFactor, 2);
  Serial.println("Copy this number into calibrationFactor at the top of the sketch");
  Serial.println("so you do not need to calibrate again after a restart.");
  Serial.println("Remove the weight and send t to tare again.");
}