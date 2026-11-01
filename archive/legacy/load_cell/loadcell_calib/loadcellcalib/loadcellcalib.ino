// #include "HX711.h"

// const int LOADCELL_DOUT_PIN = 3;
// const int LOADCELL_SCK_PIN = 4;

// HX711 scale;

// void setup() {
//   Serial.begin(115200);
//   scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

//   while (!scale.is_ready()) {
//     delay(10);
//   }

//   Serial.println("Reading RAW 24-bit ADC values...");
// }

// void loop() {
//   if (scale.is_ready()) {
//     long raw = scale.read();
//     Serial.print("Raw ADC: ");
//     Serial.println(raw);
//   }
//   delay(200);
// }
// ----------------------------------------------------------------------------
#include "HX711.h"

const int LOADCELL_DOUT_PIN = 3;
const int LOADCELL_SCK_PIN = 4;

HX711 scale;

// Calibration factor = (Raw ADC count difference) / (weight in mg).
// If 48400 was your factor for 1 kg (1,000,000 mg), 
// the equivalent milligram factor is approximately 48400 / 1000000 = 0.0484.
float calibration_factor = 0.0384; 

void setup() {
  Serial.begin(115200); // 115200 baud avoids serial lag
  scale.begin(LOADCELL_DOUT_PIN, LOADCELL_SCK_PIN);

  // Wait until HX711 is stable and ready
  while (!scale.is_ready()) {
    delay(10);
  }

  scale.set_scale();
  
  // Milligram measurements require higher sample counts to cancel noise
  Serial.println("Zeroing load cell... keep completely still.");
  scale.tare(40); 

  long zero_factor = scale.get_offset();
  Serial.print("Zero offset: ");
  Serial.println(zero_factor);
  
  Serial.println("\nCommands (Send in Serial Monitor):");
  Serial.println("  'a' / 'z' : +/- 0.0100");
  Serial.println("  'b' / 'x' : +/- 0.0010");
  Serial.println("  'c' / 'v' : +/- 0.0001");
  Serial.println("  't'       : Re-tare zero point\n");

  scale.set_scale(calibration_factor);
}

void loop() {
  // Average 15 samples to suppress air turbulence and vibration
  float reading_mg = scale.get_units(50); // Or 40-50 for high stability

  Serial.print("Reading: ");
  Serial.print(reading_mg, 1); // 1 decimal place = 0.1 mg
  Serial.print(" mg | calibration_factor: ");
  Serial.println(calibration_factor, 6);

  if (Serial.available()) {
    char temp = Serial.read();

    // Use single characters: multi-char literals like '++' or '--' are invalid in C++
    switch (temp) {
      case 'a':
        calibration_factor += 0.0100;
        break;
      case 'z':
        calibration_factor -= 0.0100;
        break;
      case 'b':
        calibration_factor += 0.0010;
        break;
      case 'x':
        calibration_factor -= 0.0010;
        break;
      case 'c':
        calibration_factor += 0.0001;
        break;
      case 'v':
        calibration_factor -= 0.0001;
        break;
      case 't':
        Serial.println("--> Taring...");
        scale.tare(30);
        break;
      default:
        // Skip CR/LF line endings
        break;
    }

    scale.set_scale(calibration_factor);
  }

  delay(300);
}