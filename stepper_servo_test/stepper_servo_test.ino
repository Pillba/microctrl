#include <Stepper.h>
#include <Servo.h>

// change this to the number of steps on your motor
#define STEPS 100

// minimum change in sensor reading before we bother stepping
// (helps avoid jittery motion from analog noise)
#define STEP_THRESHOLD 2

// create an instance of the stepper class, specifying
// the number of steps of the motor and the pins it's
// attached to
Stepper stepper(STEPS, 2, 3, 4, 5);

// the previous reading from the analog input
int previous = 0;

Servo myservo;  // create servo object to control a servo

int pos = 0;              // current servo position
bool servoGoingUp = true; // sweep direction
unsigned long lastServoMove = 0;
const int servoInterval = 15; // ms between servo steps (consistent both directions)

void setup() {
  // set the speed of the motor to 70 RPM
  stepper.setSpeed(70);
  myservo.attach(9);  // attaches the servo on pin 9 to the servo object
}

void loop() {
  // --- Stepper motor: move based on change in sensor reading ---
  int val = analogRead(0);
  int delta = val - previous;

  if (abs(delta) >= STEP_THRESHOLD) {
    stepper.step(delta);   // still blocking during the actual step, but
                            // small/thresholded deltas keep this brief
    previous = val;
  }

  // --- Servo: non-blocking sweep using millis() instead of delay() ---
  if (millis() - lastServoMove >= servoInterval) {
    lastServoMove = millis();

    if (servoGoingUp) {
      pos++;
      if (pos >= 180) {
        pos = 180;
        servoGoingUp = false;
      }
    } else {
      pos--;
      if (pos <= 0) {
        pos = 0;
        servoGoingUp = true;
      }
    }

    myservo.write(pos);
  }
}