//COMBINE 2 -- IR, PUMP, SERVO

#include <Arduino.h>
#include <Stepper.h>
#include <Servo.h>

// --- Stepper Motor Setup ---
#define STEPS 100
#define STEP_THRESHOLD 2

// Pins 2, 3, 4, 5 allocated for Stepper driver
Stepper stepper(STEPS, 2, 3, 4, 5);
int previous = 0;

// --- Servo & Pump Setup ---
Servo myservo;
const int pumpPin = 6;
const int servoPin = 9;

int pos = 0;
bool servoGoingUp = true;
unsigned long lastServoMove = 0;
int servoInterval = 5; // 5ms going up, 15ms going down

// Flag to handle manual console override
bool manualPumpControl = false;

// --- IR Sensor & Indicator LED Setup ---
const uint8_t IR_EMITTER = 12;
const uint8_t IR_RECEIVER = 8; // Pin 8 from the 2nd sketch
#define LEDPIN 13              // Built-in LED

int lastSensorState = -1;

void setup() {
  Serial.begin(9600);

  // Stepper setup
  stepper.setSpeed(70);

  // Servo setup
  myservo.attach(servoPin);

  // Pump pin setup
  pinMode(pumpPin, OUTPUT);
  digitalWrite(pumpPin, LOW);

  // IR sensor & LED pins setup
  pinMode(IR_EMITTER, OUTPUT);
  digitalWrite(IR_EMITTER, HIGH);
  pinMode(IR_RECEIVER, INPUT_PULLUP);
  pinMode(LEDPIN, OUTPUT);

  // Console instructions
  Serial.println("--- System Initialized ---");
  Serial.println("Send '1' for manual Pump ON");
  Serial.println("Send '0' for manual Pump OFF");
  Serial.println("Send 'A' to return pump to AUTO sweep mode");
  Serial.println("--------------------------");
}

void loop() {
  // 1. Read IR Sensor & update LED / Serial on state change
  int sensorState = digitalRead(IR_RECEIVER);
  digitalWrite(LEDPIN, (sensorState == HIGH) ? HIGH : LOW);

  if (sensorState != lastSensorState) {
    if (sensorState == HIGH) {
      Serial.println("[IR] Beam broken!");
    } else {
      Serial.println("[IR] Beam clear.");
    }
    lastSensorState = sensorState;
  }

  // 2. Stepper motor: position tracking based on analog pin A0
  int val = analogRead(A0);
  int delta = val - previous;

  if (abs(delta) >= STEP_THRESHOLD) {
    stepper.step(delta);
    previous = val;
  }

  // 3. Servo & Pump sweep (non-blocking)
  if (millis() - lastServoMove >= (unsigned long)servoInterval) {
    lastServoMove = millis();

    if (servoGoingUp) {
      pos++;
      servoInterval = 5; // 5ms delay going 0 -> 180
      if (!manualPumpControl) {
        digitalWrite(pumpPin, LOW); // Auto: Pump OFF sweeping up
      }

      if (pos >= 180) {
        pos = 180;
        servoGoingUp = false;
      }
    } else {
      pos--;
      servoInterval = 15; // 15ms delay going 180 -> 0
      if (!manualPumpControl) {
        digitalWrite(pumpPin, HIGH); // Auto: Pump ON sweeping down
      }

      if (pos <= 0) {
        pos = 0;
        servoGoingUp = true;
      }
    }

    myservo.write(pos);
  }

  // 4. Handle Serial Monitor Input
  if (Serial.available() > 0) {
    char consoleInput = Serial.read();

    if (consoleInput == '1') {
      manualPumpControl = true;
      digitalWrite(pumpPin, HIGH);
      Serial.println("[STATUS] Pump manually set to ON");
    } else if (consoleInput == '0') {
      manualPumpControl = true;
      digitalWrite(pumpPin, LOW);
      Serial.println("[STATUS] Pump manually set to OFF");
    } else if (consoleInput == 'A' || consoleInput == 'a') {
      manualPumpControl = false;
      Serial.println("[STATUS] Pump returned to AUTO mode");
    } else if (consoleInput != '\n' && consoleInput != '\r' && consoleInput != ' ') {
      Serial.print("[ERROR] Invalid command: '");
      Serial.print(consoleInput);
      Serial.println("'. Use '1', '0', or 'A'.");
    }
  }
}