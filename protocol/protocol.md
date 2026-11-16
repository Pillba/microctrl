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
| `0x10` | `DISPENSE_CMD` | Pi → MCU | request ID, slot, quantity, optional unit weight (mg) |
| `0x11` | `DISPENSE_RESULT` | MCU → Pi | request ID, slot, result code, confirmed quantity |
| `0x20` | `SCHEDULE_SET` | Pi → MCU | request ID, slot, time, days, active |
| `0x21` | `SCHEDULE_ACK` | MCU → Pi | request ID, slot, result |
| `0x40` | `LOADCELL_RESULT` | MCU → Pi | request ID, compartment, weight |
| `0x41` | `IR_BREAK_BEAM_RESULT` | MCU → Pi | request ID, compartment, state |
| `0x80` | `BENCH_CMD` | Pi → MCU | request ID, command, arg (production: scale calibration only) |
| `0x81` | `BENCH_RESULT` | MCU → Pi | request ID, command, status, value |
| `0xFF` | `ERROR` | Both | request ID, error code, context |

The panel-ui reference additionally defines `DOSE_EVENT`, `LOAD_SENSOR`,
`LID_STATUS`, `SETTINGS_SET`, `SETTINGS_ACK`, `DEVICE_STATUS`, and
`FACTORY_RESET`. These messages must be added to the shared registry and test
vectors when their production payloads are approved.

## Dispense payloads

```text
DISPENSE_CMD: request_id[4] + slot[1] + quantity[1]                          (6 bytes)
DISPENSE_CMD: request_id[4] + slot[1] + quantity[1] + unit_weight_mg[2]      (8 bytes)
DISPENSE_RESULT: request_id[4] + slot[1] + result_code[1] + confirmed_quantity[1]
```

`unit_weight_mg` (uint16, little-endian) is the weight of one unit in
milligrams: one pill for slots 1–6, one mL for slot 7 (650 = 0.65 g per pill,
1030 = 1.03 g/mL). `0`, or the 6-byte form, means "use the firmware's
default" (0.95 g per pill, 1.00 g/mL). The MCU accepts both lengths, so a Pi
and an MCU can be updated one at a time; the Pi sends the 6-byte form when a
medication has no weight set. Any other payload length gets no reply.

The MCU implementation is the `production` app in `firmware/` (codec in
`firmware/lib/protocol/`, sequence in `firmware/lib/dispense/`).

- **Slots 1–6 (pills), quantity = pills.** The MCU steps the slot until the
  break beam has counted `quantity` pills, at most `3 × 1000 × quantity` steps
  (6 s per pill), requires the break-beam count to reach the quantity, and checks
  the settled weight against the per-pill weight (`unit_weight_mg`, or 0.95 g)
  multiplied by the quantity (±45 %, at least 0.030 g).
- **Slot 7 (liquid), quantity = whole mL, 1–30.** The pump fills the same
  weighed cup behind the shield until it gains `mL × unit weight` (default
  1.00 g/mL). If nothing
  reaches the cup within 5 s the pump stops (`NO_WEIGHT`); it never runs longer
  than `5 s + 2 s × mL`. After a 1 s drip the settled weight must match within
  ±10 % (at least 0.5 g). The break beam is not used. The 30 mL cap protects
  the cup (`LIQUID_MAX_ML` in `firmware/lib/dispense_rules/dispense_rules.h`).

Both close the shield and tare first, and open the shield only after every
check passed. The MCU answers `BUSY` immediately (it services the UART while
the shield moves, while stepping or pumping, and while it waits for the IR beam
and the weight; only the 2 s tare does not) when a command arrives during a
dispense or a scale calibration session (see "Scale calibration"). A command
with a bad slot (outside 1–7), a quantity of 0, or more than 30 mL for slot 7
is answered with `INVALID_REQUEST`. A frame of another type, or a
`DISPENSE_CMD` whose payload is not 6 or 8 bytes, gets no reply.

`confirmed_quantity` equals the requested quantity (pills or mL) on `OK` and is
0 otherwise.

The hall sensors that confirm the shield position are **not fitted yet**
(`HALL_SENSORS_FITTED = false` in `firmware/lib/board/board.h`): the shield is
commanded and given 0.7 s to move, and nothing confirms its position.

### Result codes

| Code | Name | Meaning |
| --- | --- | --- |
| `0` | `OK` | Dispensed and verified; shield opened |
| `1` | `FAILED` | Generic failure (kept for compatibility) |
| `2` | `INVALID_REQUEST` | Slot not 1–7, quantity 0, or slot 7 above 30 mL |
| `3` | `BUSY` | A dispense or a scale calibration session is running |
| `4` | `SCALE_NOT_RESPONDING` | HX711 did not answer; nothing moved (liquid: the pump never started, or stopped at once) |
| `5` | `IR_NOT_TRIPPED` | Fewer beam breaks than the quantity (pills only) |
| `6` | `NO_WEIGHT` | Pills: beam tripped but no weight reached the cup. Liquid: nothing reached the cup within 5 s (dry reservoir, blocked tube) |
| `7` | `WEIGHT_MISMATCH` | Settled weight outside the tolerance (liquid: also when the pump ran out of time) |

Codes 4–7 are an additive refinement of the old generic `1`; the Pi treats
every nonzero code as a failure. On every failure the shield stays closed (and
the pump is off).

### Timing and the Pi's response timeout

Worst cases on the MCU, with the hall sensors fitted (without them each shield
move takes 0.7 s instead of up to 4.3 s):

- pills: about 41 s + 6 s per pill (scale check 1 s, shield 4.3 s, tare 2 s,
  steps up to 6 s per pill, IR 5.2 s, weight 5 s, settling 15 s + 2 s, shield 4.3 s);
- liquid: about 30 s + the pump budget of 5 s + 2 s per mL.

The Pi's response timeout must be `45 s + 15 s × quantity` for pills and
`50 s + 2 s × mL` for liquid (`bench_menu_frames.py` uses these). After that the
outcome is `UNKNOWN` (see below). The liquid pump budget and the Pi's liquid
timeout must change together.

**Pending approval by both contributors** (see "Frame" above): result codes
4–7, slot 7 (liquid, quantity in mL, the 30 mL cap), the scale calibration
commands below, and the 8-byte `DISPENSE_CMD` with `unit_weight_mg` (additive:
the 6-byte form keeps its meaning).

Medication records and schedules remain Pi-owned. The MCU receives commands
and reports hardware outcomes; it is not the medication database.

## Scale calibration (production)

The production firmware answers two `BENCH_CMD` commands (same payloads,
status codes and IDs as the bench frames below) so the panel's Calibrate Scale
page can calibrate the load cell without reflashing:

| Command | arg | Reply status | value |
| --- | --- | --- | --- |
| `0x08` CALIBRATE_START | – | `0` OK, `1` FAILED (HX711 does not answer), `3` BUSY | 0 |
| `0x09` CALIBRATE_FINISH | known weight in µg, `0` cancels | `0` OK, `5` UNSTEADY (saved), `7` NO_CHANGE (nothing saved), `6` CANCELLED, `2` INVALID (no START, the MCU restarted, or the session expired), `1` FAILED, `3` BUSY | factor × 100 |

START zeroes the empty scale in raw mode (about 10 s). Until FINISH the scale
reads raw values, so every `DISPENSE_CMD` is answered `BUSY`. FINISH measures
the known weight (about 35 s) and saves the factor in EEPROM. A session that is
not finished within 10 minutes is cancelled (the saved factor is restored).
While a dispense runs, both commands are answered `BUSY`. Every other
`BENCH_CMD` command gets no reply from production.

## Bench frames (bench firmware only)

`firmware/bench/dispense_UI` also accepts the rest of the `dispense.cpp` bench
menu as frames, driven by `pi/tests/integration/bench_menu_frames.py`. These
IDs are reserved for bench use and are not part of the production contract;
the `production` app answers only CALIBRATE_START and CALIBRATE_FINISH (see
"Scale calibration") and ignores the other commands. Integers are
little-endian; weights are in micrograms.

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
the stream. The production firmware writes only frames, except in a `DEBUG=1`
build (firmware/README.md), which adds `DBG ...` text lines between frames in
the same way. Receivers must skip bytes outside frames.

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
