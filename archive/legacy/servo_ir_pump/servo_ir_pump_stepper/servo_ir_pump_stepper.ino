// Code used to demo 

#include <Arduino.h>
#include <Servo.h>

// --- Stepper Driver (from Script 2, replacing Servo) ---
const int STEP_PIN = 8;
const int DIR_PIN  = 10;
const int RST_PIN  = 13;
const int EN_PINS[6] = {A0, A1, A2, A3, A4, A5};

const int TEST_BOARD    = 1;    // 0 = StepperBoard1, 1 = StepperBoard2, ... 5 = StepperBoard6
const int STEPS         = 200;
const int STEP_DELAY_MS = 5;

// Servo Setup
Servo myservo;
const int servoPin = 9;
int pos = 0;
bool servoGoingUp = true;
unsigned long lastServoMove = 0;
int servoInterval = 5; // 5ms going up, 15ms going down

// Non-blocking state machine for the stepper sequence
enum StepperState {
    STEPPER_FWD,
    STEPPER_PAUSE_1,
    STEPPER_REV,
    STEPPER_PAUSE_2,
    STEPPER_DISABLED_PULSE,
    STEPPER_PAUSE_3
};

StepperState stepperState = STEPPER_FWD;
unsigned long stepperStateStartTime = 0;

// Pulse generation sub-state
int stepsCompleted = 0;
unsigned long lastStepTime = 0;

// --- Pump Setup ---
const int pumpPin = 6;
bool manualPumpControl = false;

// --- IR Sensor Setup ---
// Shifted receiver to Pin 7 to free Pin 8 for STEP_PIN
const uint8_t IR_EMITTER  = 12;
const uint8_t IR_RECEIVER = 2;
int lastSensorState = -1;

void stepMotorNonBlocking(int targetSteps, StepperState nextState) {
    if (stepsCompleted < targetSteps) {
        if (millis() - lastStepTime >= (unsigned long)STEP_DELAY_MS) {
            lastStepTime = millis();
            digitalWrite(STEP_PIN, HIGH);
            delayMicroseconds(10);
            digitalWrite(STEP_PIN, LOW);
            stepsCompleted++;
        }
    } else {
        stepsCompleted = 0;
        stepperState = nextState;
        stepperStateStartTime = millis();
    }
}

void setup() {
    Serial.begin(9600);

    // Stepper driver pins (Script 2)
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN, OUTPUT);
    digitalWrite(STEP_PIN, LOW);

    pinMode(RST_PIN, OUTPUT);
    digitalWrite(RST_PIN, HIGH); // Power source for stepper board 5V line

    for (int i = 0; i < 6; i++) {
        pinMode(EN_PINS[i], OUTPUT);
        digitalWrite(EN_PINS[i], HIGH); // Disable all boards initially
    }

    // Pump setup
    pinMode(pumpPin, OUTPUT);
    digitalWrite(pumpPin, LOW);

    // Servo Setup
    myservo.attach(servoPin);

    // IR sensor setup
    pinMode(IR_EMITTER, OUTPUT);
    digitalWrite(IR_EMITTER, HIGH);
    pinMode(IR_RECEIVER, INPUT_PULLUP);

    // Initialize first stepper motion state
    digitalWrite(EN_PINS[TEST_BOARD], LOW);
    digitalWrite(DIR_PIN, HIGH);
    if (!manualPumpControl) digitalWrite(pumpPin, LOW);

    Serial.println("--- System Initialized ---");
    Serial.println("Send '1' for manual Pump ON");
    Serial.println("Send '0' for manual Pump OFF");
    Serial.println("Send 'A' to return pump to AUTO mode");
    Serial.println("Note: IR_RECEIVER moved to Pin 7");
    Serial.println("--------------------------");
}

void loop() {
    // 1. Read IR Sensor & output state changes
    int sensorState = digitalRead(IR_RECEIVER);
    if (sensorState != lastSensorState) {
        if (sensorState == HIGH) {
            Serial.println("[IR] Beam broken!");
        } else {
            Serial.println("[IR] Beam clear.");
        }
        lastSensorState = sensorState;
    }

    // 2. Non-blocking Stepper Cycle (replaces Servo sweep)
    switch (stepperState) {
        case STEPPER_FWD:
            stepMotorNonBlocking(STEPS, STEPPER_PAUSE_1);
            break;

        case STEPPER_PAUSE_1:
            if (millis() - stepperStateStartTime >= 1000) {
                digitalWrite(DIR_PIN, LOW);
                if (!manualPumpControl) digitalWrite(pumpPin, HIGH); // Matches down-sweep auto behavior
                stepperState = STEPPER_REV;
            }
            break;

        case STEPPER_REV:
            stepMotorNonBlocking(STEPS, STEPPER_PAUSE_2);
            break;

        case STEPPER_PAUSE_2:
            if (millis() - stepperStateStartTime >= 1000) {
                digitalWrite(EN_PINS[TEST_BOARD], HIGH); // Disable board
                stepperState = STEPPER_DISABLED_PULSE;
            }
            break;

        case STEPPER_DISABLED_PULSE:
            stepMotorNonBlocking(STEPS, STEPPER_PAUSE_3);
            break;

        case STEPPER_PAUSE_3:
            if (millis() - stepperStateStartTime >= 2000) {
                // Loop back to start
                digitalWrite(EN_PINS[TEST_BOARD], LOW); // Enable board
                digitalWrite(DIR_PIN, HIGH);
                if (!manualPumpControl) digitalWrite(pumpPin, LOW);
                stepperState = STEPPER_FWD;
            }
            break;
    }

       // 3. Servo & Pump sweep (non-blocking)
    if (millis() - lastServoMove >= (unsigned long)servoInterval)
    {
        lastServoMove = millis();

        if (servoGoingUp)
        {
            pos++;
            servoInterval = 5; // 5ms delay going 0 -> 180
            if (!manualPumpControl)
            {
                digitalWrite(pumpPin, LOW); // Auto: Pump OFF sweeping up
            }

            if (pos >= 180)
            {
                pos = 180;
                servoGoingUp = false;
            }
        }
        else
        {
            pos--;
            servoInterval = 15; // 15ms delay going 180 -> 0
            if (!manualPumpControl)
            {
                digitalWrite(pumpPin, HIGH); // Auto: Pump ON sweeping down
            }

            if (pos <= 0)
            {
                pos = 0;
                servoGoingUp = true;
            }
        }

        myservo.write(pos);
    }

    // 3. Handle Serial Monitor Input
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