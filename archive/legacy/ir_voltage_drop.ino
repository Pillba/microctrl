/*
Pillba - IR Voltage-Drop Detection Test (Arduino sketch, bench prototype)
Wiring:
IR phototransistor/receiver analog output -> A0
pin idles at a known voltage when the beam is unbroken)
*/

const int IR_PIN = A0;
const int THRESHOLD_DROP = 100;  // Tune during testing

int baseline = 0;

void setup() {
  Serial.begin(57600);
  Serial.println("Pillba IR Voltage-Drop Test");

  Serial.println("Reading baseline with beam clear...");
  delay(500);
  baseline = analogRead(IR_PIN);
}

void loop() {
  int reading = analogRead(IR_PIN);
  bool broken = (baseline - reading) > THRESHOLD_DROP && reading < baseline;

  Serial.print("ADC=");
  Serial.print(reading);
  Serial.print(" baseline=");
  Serial.print(baseline);
  Serial.println(broken ? " BROKEN" : " clear");

  delay(100);
}
