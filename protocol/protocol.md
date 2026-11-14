# Pi–MCU Protocol

This document is the contract between the Raspberry Pi and MCU. Implementations
must conform to this document and the byte-level vectors in
[`../protocol/test_vectors/`](../protocol/test_vectors/).

The current Pi implementation reference is
`/Users/Owner/workspace-school/sd1/panel-ui/schema/uart_protocol.py` and
`serial_worker.py`. Its parser already implements incremental sync recovery,
payload-length validation, and CRC-8/MAXIM validation.

## Frame

The initial wire format is:

```text
AA 55 | message_type | payload_length | payload | CRC8
```

Values are little-endian where a multi-byte numeric field is specified. The
maximum payload is 64 bytes. Serial settings are `115200 8N1`. The checksum is
CRC-8/MAXIM.

Protocol versioning is intentionally deferred. Until a versioning decision is
made, prefer additive message and payload changes and do not silently change
the meaning of an existing field. Any breaking change requires both
contributors' approval and a documented migration decision.

## Message registry

| ID | Name | Direction | Payload |
| --- | --- | --- | --- |
| `0x01` | `HEARTBEAT` | MCU → Pi | request ID + device status |
| `0x02` | `HEARTBEAT_ACK` | Pi → MCU | request ID |
| `0x10` | `DISPENSE_CMD` | Pi → MCU | request ID, slot, quantity |
| `0x11` | `DISPENSE_RESULT` | MCU → Pi | request ID, slot, result code, confirmed quantity |
| `0x20` | `SCHEDULE_SET` | Pi → MCU | request ID, slot, time, days, active |
| `0x21` | `SCHEDULE_ACK` | MCU → Pi | request ID, slot, result |
| `0x40` | `LOADCELL_RESULT` | MCU → Pi | request ID, compartment, weight |
| `0x41` | `IR_BREAK_BEAM_RESULT` | MCU → Pi | request ID, compartment, state |
| `0xFF` | `ERROR` | Both | request ID, error code, context |

The panel-ui reference additionally defines `DOSE_EVENT`, `LOAD_SENSOR`,
`LID_STATUS`, `SETTINGS_SET`, `SETTINGS_ACK`, `DEVICE_STATUS`, and
`FACTORY_RESET`. These messages must be added to the shared registry and test
vectors when their production payloads are approved.

## Dispense payloads

```text
DISPENSE_CMD: request_id[4] + slot[1] + quantity[1]
DISPENSE_RESULT: request_id[4] + slot[1] + result_code[1] + confirmed_quantity[1]
```

The MCU implementation is the `production` app in `firmware/` (codec in
`firmware/lib/protocol/`, sequence in `firmware/lib/dispense/`). It runs
`1000 × quantity` steps on the slot, requires the break-beam count to reach the
quantity, and checks the settled weight against the expected per-pill weight
multiplied by the quantity. A command that arrives while a dispense is running
is answered immediately with `BUSY`; a command with a bad slot (outside 1–6)
or a quantity of 0 is answered with `INVALID_REQUEST`. A frame of another type,
or a `DISPENSE_CMD` with the wrong payload length, gets no reply.

`confirmed_quantity` equals the requested quantity on `OK` and is 0 otherwise.

### Result codes

| Code | Name | Meaning |
| --- | --- | --- |
| `0` | `OK` | Dispensed and verified; shield opened |
| `1` | `FAILED` | Generic failure (kept for compatibility) |
| `2` | `INVALID_REQUEST` | Slot not 1–6 or quantity 0 |
| `3` | `BUSY` | Another dispense is running |
| `4` | `SCALE_NOT_RESPONDING` | HX711 did not answer; nothing moved |
| `5` | `IR_NOT_TRIPPED` | Fewer beam breaks than the quantity |
| `6` | `NO_WEIGHT` | Beam tripped but no weight reached the cup |
| `7` | `WEIGHT_MISMATCH` | Settled weight outside the tolerance |

Codes 4–7 are an additive refinement of the old generic `1`; the Pi treats
every nonzero code as a failure. On every failure the shield stays closed.
**Pending approval by both contributors** (see "Frame" above).

A worst-case dispense of one pill takes about 30 s (shield 4 s, steps 2 s,
IR 5 s, weight 5 s, settling 15 s) and grows with quantity. The Pi's response
timeout must be longer than this, or the outcome becomes `UNKNOWN`.

Medication records and schedules remain Pi-owned. The MCU receives commands
and reports hardware outcomes; it is not the medication database.

## Bench frames (bench firmware only)

`firmware/bench/dispense_UI` also accepts the rest of the `dispense.cpp` bench
menu as frames, driven by `pi/tests/integration/bench_menu_frames.py`. These
IDs are reserved for bench use and are not part of the production contract;
the `production` app ignores them. Integers are little-endian; weights are in
micrograms.

| ID | Name | Direction | Payload |
| --- | --- | --- | --- |
| `0x80` | `BENCH_CMD` | Pi → MCU | request_id[4] + command[1] + arg[4, uint32] |
| `0x81` | `BENCH_RESULT` | MCU → Pi | request_id[4] + command[1] + status[1] + value[4, int32] |
| `0x82` | `BENCH_EVENT` | MCU → Pi | `00 00 00 00` + event[1] + value[4, int32] |

| Command | Menu | arg | value |
| --- | --- | --- | --- |
| `0x01` STEPPER_LOOP | `1`…`6` | slot | slot |
| `0x02` STOP | `stop` | – | – |
| `0x03` SHIELD | `9` / `8` | 1 open, 0 closed | same |
| `0x04` PUMP | `pon` / `poff` | 1 on, 0 off | same |
| `0x05` TARE | `t` | – | – |
| `0x06` NOISE_TEST | `r` | – | raw spread of 50 readings |
| `0x07` READ_WEIGHT | `w` | – | µg |
| `0x08` CALIBRATE_START | `c` step 1 (scale empty) | – | – |
| `0x09` CALIBRATE_FINISH | `c` step 2 | known weight µg, 0 cancels | factor × 100 |
| `0x0A` EXPECTED_WEIGHT | `e [grams]` | µg, 0 only reads | µg |

`test N` is the regular `DISPENSE_CMD` with quantity 1.

Status: `0` OK, `1` FAILED (HX711 does not answer), `2` INVALID, `3` BUSY,
`4` NOT_CALIBRATED, `5` UNSTEADY (calibration saved), `6` CANCELLED,
`7` NO_CHANGE (calibration not saved).

Events: `0x01` PLAY_SOUND (value: sound number), `0x02` HALL (0 between,
1 closed, 2 open, 3 both sensors), `0x03` READY (bit 0 scale calibrated,
bit 1 clear beam reads HIGH). Dispense progress is still logged as plain ASCII
text lines between frames; ASCII never contains `0xAA`, so the Pi can split
the stream.

## Request IDs and failure semantics

Nonzero request IDs correlate a response with one operation. An all-zero
request ID (`00 00 00 00`) identifies an asynchronous event.

The Pi performs no automatic retries. A timeout, disconnect, MCU reset, or
malformed response after a command is sent produces an `UNKNOWN` outcome, not
an assumed failure. The original operation ID is retained for reconciliation.

An operator must explicitly choose one of:

- confirm dispensed;
- confirm not dispensed;
- retry using the same operation ID;
- inspect sensor/load-cell evidence when available.

Unknown operations are visible as blocking alerts and are never silently
retried. See [`database.md`](database.md) for inventory accounting.
