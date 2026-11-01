// Your pin configuration
#include <Servo.h>
Servo myservo;  // create servo object to control a servo

const uint8_t IR_EMITTER = 12;   
const uint8_t IR_RECEIVER = 8;
int pos = 0;    // variable to store the servo position


#define LEDPIN 13 // Built-in LED for visual testing

void setup() {
  // Set the emitter pin as an output and turn it constantly ON
  pinMode(IR_EMITTER, OUTPUT);
  digitalWrite(IR_EMITTER, HIGH); 
  
  // Set the receiver pin using the internal pull-up resistor
  pinMode(IR_RECEIVER, INPUT_PULLUP);
  
  pinMode(LEDPIN, OUTPUT);
  // myservo.attach(9);  // attaches the servo on pin 9 to the servo object

  Serial.begin(9600);
}

void loop() {
  int sensorState = digitalRead(IR_RECEIVER);

  // HIGH = Beam is broken (light blocked)
  if (sensorState == HIGH) {
    digitalWrite(LEDPIN, HIGH);     // Turn built-in LED on
    Serial.println("Beam broken!");
  } 
  // LOW = Beam is clear (light received)
  else {
    digitalWrite(LEDPIN, LOW);      // Turn built-in LED off
    Serial.println("Beam clear.");
  }
  // for (pos = 0; pos <= 180; pos += 1) { // goes from 0 degrees to 180 degrees
  //   // in steps of 1 degree
  //   myservo.write(pos);              // tell servo to go to position in variable 'pos'
  //   delay(5);                       // waits 15ms for the servo to reach the position
  // }
  // for (pos = 180; pos >= 0; pos -= 1) { // goes from 180 degrees to 0 degrees
  //   myservo.write(pos);              // tell servo to go to position in variable 'pos'
  //   delay(15);                       // waits 15ms for the servo to reach the position
  // }
  // delay(10000); // Small stabilization delay
}
