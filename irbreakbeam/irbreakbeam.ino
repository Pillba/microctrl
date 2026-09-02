// Your pin configuration
const uint8_t IR_EMITTER = 11;   
const uint8_t IR_RECEIVER = 8;

#define LEDPIN 13 // Built-in LED for visual testing

void setup() {
  // Set the emitter pin as an output and turn it constantly ON
  pinMode(IR_EMITTER, OUTPUT);
  digitalWrite(IR_EMITTER, HIGH); 
  
  // Set the receiver pin using the internal pull-up resistor
  pinMode(IR_RECEIVER, INPUT_PULLUP);
  
  pinMode(LEDPIN, OUTPUT);
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
  
  delay(10000); // Small stabilization delay
}
