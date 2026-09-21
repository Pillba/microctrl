/*
 * Pillba - DRV8825 Stepper Driver Test
 *
 * Wiring (DRV8825 breakout):
 *   STEP  -> 
 *   DIR   -> 
 *   ENABLE->    (DRV8825 ENABLE is active-LOW: LOW = driver on)
 */

const int STEP_PIN = 2;
const int DIR_PIN = 3;
const int ENABLE_PIN = 4;

// full steps per faceplate revolution: set to match the real gear ratio

const int STEPS_PER_REV = 200;

void setup() {
  Serial.begin(57600);
  Serial.println("Pillba DRV8825 Stepper Test");

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);

  digitalWrite(ENABLE_PIN, HIGH);  // start disabled
  digitalWrite(ENABLE_PIN, LOW);   // active-low: LOW = driver on
}

void step(int delayUs) {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(2);  // DRV8825 minimum STEP high time is ~1.9us
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(delayUs);
}

void moveSteps(int count, bool forward, int stepDelayUs) {
  digitalWrite(DIR_PIN, forward ? HIGH : LOW);
  for (int i = 0; i < count; i++) {
    step(stepDelayUs);
  }
}

void loop() {
  Serial.println("Indexing one compartment forward");
  moveSteps(STEPS_PER_REV / 8, true, 1500);  // 1/8 turn = one of 8 compartments
  delay(1000);

  Serial.println("Indexing one compartment back");
  moveSteps(STEPS_PER_REV / 8, false, 1500);
  delay(1000);
}
