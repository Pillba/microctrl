# Pillba Microcontroller

---

Stored In this repository is PlatformIO projects and Arduino sketches for
controlling peripherals via the Arduino ATmega328p.

Peripherals Include:

- Stepper motor
- Servo motor
- Loadcell
- Pump motor
- Infared Break Beam
- Hall Sensor

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

### Frame format

```text
AA 55 | message_type | payload_length | payload | CRC8
```

- Baud: `115200 8N1`
- CRC: CRC-8/MAXIM
- Maximum payload: `64 bytes`
- Little-endian numeric fields
- Request/response commands include a 4-byte `request_id`

### Proposed command schema

| ID     | Name                   | Direction | Payload                              |
| ------ | ---------------------- | --------- | ------------------------------------ |
| `0x01` | `HEARTBEAT`            | MCU → Pi  | request ID + device status           |
| `0x02` | `HEARTBEAT_ACK`        | Pi → MCU  | request ID                           |
| `0x10` | `DISPENSE_CMD`         | Pi → MCU  | request ID, slot, quantity           |
| `0x11` | `DISPENSE_RESULT`      | MCU → Pi  | request ID, slot, result             |
| `0x20` | `SCHEDULE_SET`         | Pi → MCU  | request ID, slot, time, days, active |
| `0x21` | `SCHEDULE_ACK`         | MCU → Pi  | request ID, slot, result             |
| `0x40` | `LOADCELL_RESULT`      | MCU → Pi  | request ID, compartment, weight      |
| `0x41` | `IR_BREAK_BEAM_RESULT` | MCU → Pi  | request ID, compartment, state       |
| `0xFF` | `ERROR`                | Both      | request ID, error code, context      |

### Existing dispense format

The successful test currently sends:

```text
request_id[4] + slot[1] + quantity[1]
```

The response currently expects:

```text
request_id[4] + slot[1] + result_code[1]
```

So its payload is 6 bytes, matching `uart_connection_test.py` and
`serial_worker.py`.

### Request ID rules

- Polled request: MCU echoes the Pi’s 4-byte `request_id`.
- Asynchronous event: MCU uses `request_id = 00 00 00 00`.
- Existing dispense payload remains:

```
REQ_ID[4] + SLOT[1] + QUANTITY[1]
```

- Existing dispense result remains:

```
REQ_ID[4] + SLOT[1] + RESULT[1]
```

### Routing behavior

`SerialWorker` should:

1. Parse the common frame format.
2. Route `00 00 00 00` messages to an asynchronous-event signal.
3. Match nonzero request IDs to the pending command.
4. Reject responses with an unexpected request ID.
