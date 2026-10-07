// Load cell with no preset scale value. The scale is calculated by weighing.
//
// Steps (Serial Monitor at 115200, line ending Newline):
//   1. Power up with the scale empty. It tares automatically.
//   2. Place a known weight on the scale.
//   3. Type the weight (for example 100) and press Enter.
//   4. It calculates counts per unit and then prints live weight.
// Units are whatever you type: grams if you enter grams.

const uint8_t HX_DT = 12;
const uint8_t HX_SCK = 10;

long tareCounts = 0;
float scale = 1.0f;               // counts per unit, found during calibration

long readRaw() {
  while (digitalRead(HX_DT) == HIGH) {}      // wait until the HX711 is ready
  noInterrupts();                            // keep SCK timing clean
  unsigned long v = 0;
  for (uint8_t i = 0; i < 24; i++) {
    digitalWrite(HX_SCK, HIGH);
    v = (v << 1) | digitalRead(HX_DT);
    digitalWrite(HX_SCK, LOW);
  }
  digitalWrite(HX_SCK, HIGH);                // 25th pulse: channel A, gain 128
  digitalWrite(HX_SCK, LOW);
  interrupts();
  if (v & 0x800000UL) v |= 0xFF000000UL;     // sign extend
  return (long)v;
}

long readAverage(uint8_t n) {
  long long sum = 0;
  for (uint8_t i = 0; i < n; i++) sum += readRaw();
  return (long)(sum / n);
}

void setup() {
  Serial.begin(115200);
  pinMode(HX_DT, INPUT);
  pinMode(HX_SCK, OUTPUT);
  digitalWrite(HX_SCK, LOW);

  Serial.println("Keep the scale empty, taring...");
  tareCounts = readAverage(30);
  Serial.print("Tare counts: ");
  Serial.println(tareCounts);

  Serial.println("Place a known weight, type its value, press Enter");
  float known = 0;
  while (known <= 0) {
    while (!Serial.available()) {}
    known = Serial.parseFloat();
    while (Serial.available()) Serial.read();   // clear leftover newline
    if (known <= 0) Serial.println("Enter a number greater than 0");
  }

  Serial.println("Measuring...");
  long loaded = readAverage(30);
  scale = (float)(loaded - tareCounts) / known;

  Serial.print("Loaded counts: ");
  Serial.println(loaded);
  Serial.print("Scale (counts per unit): ");
  Serial.println(scale, 4);
  Serial.println("Copy this scale value into your sketches. Live readings:");
}

void loop() {
  float units = (readAverage(10) - tareCounts) / scale;
  Serial.print("units: ");
  Serial.println(units, 2);
  delay(500);
}