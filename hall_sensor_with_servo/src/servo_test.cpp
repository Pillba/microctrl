#include <Servo.h>
#include <Arduino.h>

Servo myservo; // create servo object to control a servo
// twelve servo objects can be created on most boards

int pos = 0; // variable to store the servo position

// Hall Effect Sensor wiring
const int HALL_SENSOR_PIN = A3;
const float ARDUINO_VCC = 5.0; // Change to 3.3 if using a 3.3V board

void setup()
{
    myservo.attach(9); // attaches the servo on pin 9 to the servo object
    Serial.begin(115200);
    Serial.println("Hall Effect Sensor Voltage Demo");

    pinMode(HALL_SENSOR_PIN, INPUT);
}

void loop()
{
    // for (pos = 0; pos <= 180; pos += 1)
    // { // goes from 0 degrees to 180 degrees
    //     // in steps of 1 degree
    //     myservo.write(pos); // tell servo to go to position in variable 'pos'
    //     delay(5);           // waits 15ms for the servo to reach the position
    // }
    // for (pos = 180; pos >= 0; pos -= 1)
    // {                       // goes from 180 degrees to 0 degrees
    //     myservo.write(pos); // tell servo to go to pos ition in variable 'pos'
    //     delay(15);          // waits 15ms for the servo to reach the position
    // }

    // Read analog value (0 - 1023)
    int rawHall = analogRead(HALL_SENSOR_PIN);

    // Convert raw ADC reading to voltage
    float hallVoltage = (rawHall * ARDUINO_VCC) / 1023.0;

    // Print results
    Serial.print("Hall ADC: ");
    Serial.print(rawHall);
    Serial.print("\t| Voltage: ");
    Serial.print(hallVoltage, 3);
    Serial.println(" V");

    if (hallVoltage == 0)
    {
        myservo.write(180);
        delay(3000);
        myservo.write(0);
    }

    delay(500);
}

// #include <Arduino.h>

// // Hall Effect Sensor wiring
// const int HALL_SENSOR_PIN = A3;
// const float ARDUINO_VCC = 5.0; // Change to 3.3 if using a 3.3V board

// void setup()
// {
//     Serial.begin(115200);
//     Serial.println("Hall Effect Sensor Voltage Demo");

//     pinMode(HALL_SENSOR_PIN, INPUT);
// }

// void loop()
// {
//     // Read analog value (0 - 1023)
//     int rawHall = analogRead(HALL_SENSOR_PIN);

//     // Convert raw ADC reading to voltage
//     float hallVoltage = (rawHall * ARDUINO_VCC) / 1023.0;

//     // Print results
//     Serial.print("Hall ADC: ");
//     Serial.print(rawHall);
//     Serial.print("\t| Voltage: ");
//     Serial.print(hallVoltage, 3);
//     Serial.println(" V");

//     delay(500);
// }