/*
 * Pillba - IR Break-Beam Sensor Test
 * Subsystem 3 in BOM: IR Sensor (555 timer emitter + TSOP38238 receiver)
 *
 * WIRING:
 *   TSOP38238 OUT -> D7
 *   TSOP38238 VCC -> 5V
 *   TSOP38238 GND -> GND
 *
 *   The 555 timer emitter circuit runs on its own, driven by the astable
 *   oscillator on the IR sensor board - it doesn't need an MCU pin, it's
 *   always blinking the IR LED at ~38kHz as long as it has power.
 *
 * WHAT THIS TESTS:
 *   The TSOP output is HIGH (idle) as long as the beam is unbroken, and
 *   drops LOW when something blocks it. This just watches that pin and
 *   prints when the beam gets broken and when it clears again.
 */

const int IR_PIN = 7;

// --- Optional: LED indicator, comment this whole block out if you don't
// need it or don't have an LED wired up ---
const int LED_PIN = 13;
bool useLED = true;
// --- end optional LED block ---

void setup() {
  Serial.begin(57600);
  Serial.println("Pillba IR Break-Beam Test");

  pinMode(IR_PIN, INPUT);

  // --- Optional: LED indicator ---
  if (useLED) {
    pinMode(LED_PIN, OUTPUT);
  }
  // --- end optional LED block ---
}

void loop() {
  int beamState = digitalRead(IR_PIN);

  if (beamState == LOW) {
    Serial.println("Beam broken - pill detected");

    // --- Optional: LED indicator, turns on while beam is broken ---
    if (useLED) {
      digitalWrite(LED_PIN, HIGH);
    }
    // --- end optional LED block ---

  } else {
    Serial.println("Beam clear");

    // --- Optional: LED indicator, off while beam is clear ---
    if (useLED) {
      digitalWrite(LED_PIN, LOW);
    }
    // --- end optional LED block ---
  }

  delay(200);
}