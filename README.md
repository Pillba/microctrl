# Pillba Microcontroller

---

Stored In this repository is PlatformIO projects and Arduino sketches for controlling peripherals via the Arduino ATmega328p.

Peripherals Include:

* Stepper motor
* Servo motor
* Loadcell
* Pump motor
* Infared Break Beam
* Hall Sensor

## Pin layout

```C
#define IR_OUT		2
#define HALL1		3
#define HALL2		4
#define PUMP1		5
#define PUMP2		6
#define HX_DOUT_A	7
#define STEP_PIN	8
#define SERVO_PWM	9
#define DIR_PIN		10
#define HX_SCK		11
#define IR_EN		12
#define STPR_RST	13
#define nENBL1		A0
#define nENBL2		A1
#define nENBL3		A2
#define nENBL4		A2
#define nENBL5		A2
#define nENBL6		A2
#define HX_DOUT_B	A6
```
