/*
  Control Peristaltic Pump via Arduino Serial Monitor Console
*/

// Define the pin connected to your MOSFET, relay, or motor driver
const int pumpPin = 7; 

void setup() {
  // Set the pump control pin as an output
  pinMode(pumpPin, OUTPUT);
  
  // Start with the pump completely turned OFF
  digitalWrite(pumpPin, LOW); 
  
  // Open the serial communication console at 9600 baud rate
  Serial.begin(9600); 
  
  // Print initial instructions to the console
  Serial.println("--- Pump Console Control Ready ---");
  Serial.println("Type '1' and press Enter to turn ON.");
  Serial.println("Type '0' and press Enter to turn OFF.");
  Serial.println("----------------------------------");
}

void loop() {
  // Check if you have typed a character into the console window
  if (Serial.available() > 0) {
    
    // Read the single incoming character from the console
    char consoleInput = Serial.read(); 

    // If the input is '1', turn the pump ON
    if (consoleInput == '1') {
      digitalWrite(pumpPin, HIGH);
      Serial.println("[STATUS] Pump turned ON");
    } 
    
    // If the input is '0', turn the pump OFF
    else if (consoleInput == '0') {
      digitalWrite(pumpPin, LOW);
      Serial.println("[STATUS] Pump turned OFF");
    }
    
    // Ignore line endings, carriage returns, or spaces
    else if (consoleInput == '\n' || consoleInput == '\r' || consoleInput == ' ') {
      // Do nothing for white spaces or layout breaks
    }
    
    // Handle invalid inputs
    else {
      Serial.print("[ERROR] Invalid command received: '");
      Serial.print(consoleInput);
      Serial.println("'. Please use '1' or '0'.");
    }
  }
}
