// Program to view output of Loadcell

#include "HX711.h"

const int LOADCELL_DOUT_PIN = 3;
const int LOADCELL_SCK_PIN = 4;

HX711 scale;

const float CALIBRATION_FACTOR = 0.03355; 

// Increase window to 15 for tighter smoothing
const uint8_t WINDOW_SIZE = 15;
long raw_history[WINDOW_SIZE];
uint8_t history_idx = 0;
bool buffer_filled = false;

long getTrimmedMedianMean(long newVal) {
  raw_history[history_idx] = newVal;
  history_idx = (history_idx + 1) % WINDOW_SIZE;
  if (history_idx == 0) buffer_filled = true;

  uint8_t count = buffer_filled ? WINDOW_SIZE : history_idx;
  if (count < 5) return newVal;

  long sorted[WINDOW_SIZE];
  for (uint8_t i = 0; i < count; i++) sorted[i] = raw_history[i];

  for (uint8_t i = 1; i < count; i++) {
    long key = sorted[i];
    int8_t j = i - 1;
    while (j >= 0 && sorted[j] > key) {
      sorted[j + 1] = sorted[j];
      j--;
    }
    sorted[j + 1] = key;
  }

  // Trim top 3 and bottom 3, average middle 9 samples
  long sum = 0;
  for (uint8_t i = 3; i < count - 3; i++) {
    sum += sorted[i];
  }
  return sum / (count - 6);
}

long zero_offset = 0;

void setup() {
  Serial.begin(115200);
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  while (!scale.is_ready()) delay(10);

  Serial.println("Stabilizing buffer...");
  for (uint8_t i = 0; i < WINDOW_SIZE; i++) {
    while (!scale.is_ready());
    getTrimmedMedianMean(scale.read());
  }

  Serial.println("Capturing stable zero baseline...");
  long tare_accum = 0;
  for (uint8_t i = 0; i < 30; i++) {
    while (!scale.is_ready());
    tare_accum += getTrimmedMedianMean(scale.read());
    delay(20);
  }
  zero_offset = tare_accum / 30;

  Serial.print("Locked Zero Offset: ");
  Serial.println(zero_offset);
}

void loop() {
  if (scale.is_ready()) {
    long raw_instant = scale.read();
    long filtered_raw = getTrimmedMedianMean(raw_instant);

    long delta = filtered_raw - zero_offset;

    // --- SLOW ZERO DRIFT TRACKER ---
    // If delta is small (< 150 counts ~= 4.5g), slowly adapt baseline to kill thermal drift
    if (abs(delta) < 150) {
      // 0.02 tracking rate gently pulls the zero point
      zero_offset += (delta > 0) ? 1 : -1;
    }

    float weight_mg = (float)delta / CALIBRATION_FACTOR;

    // --- DEADBAND ---
    // Force clean 0 when within +/- 150 mg noise floor
    if (abs(weight_mg) < 150.0) {
      weight_mg = 0.0;
    }

    Serial.print("Filtered: ");
    Serial.print(filtered_raw);
    Serial.print(" | Zero: ");
    Serial.print(zero_offset);
    Serial.print(" | Weight: "); 
    Serial.print(weight_mg, 0);
    Serial.print(" mg (");
    Serial.print(weight_mg / 1000.0, 2);
    Serial.println(" g)");
  }

  // Allow serial tare on 't'
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 't') {
      zero_offset = getTrimmedMedianMean(scale.read());
      Serial.println("Manual Tare Triggered.");
    }
  }
}