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
| `0x11` | `DISPENSE_RESULT` | MCU → Pi | request ID, slot, result |
| `0x20` | `SCHEDULE_SET` | Pi → MCU | request ID, slot, time, days, active |
| `0x21` | `SCHEDULE_ACK` | MCU → Pi | request ID, slot, result |
| `0x40` | `LOADCELL_RESULT` | MCU → Pi | request ID, compartment, weight |
| `0x41` | `IR_BREAK_BEAM_RESULT` | MCU → Pi | request ID, compartment, state |
| `0xFF` | `ERROR` | Both | request ID, error code, context |

The panel-ui reference additionally defines `DOSE_EVENT`, `LOAD_SENSOR`,
`LID_STATUS`, `SETTINGS_SET`, `SETTINGS_ACK`, `DEVICE_STATUS`, and
`FACTORY_RESET`. These messages must be added to the shared registry and test
vectors when their production payloads are approved.

## Contract reconciliation gate

The repository README and panel-ui reference do not currently describe the
same dispense payload. The panel-ui implementation sends:

```text
DISPENSE_CMD: request_id[4] + slot[1] + quantity[1]
DISPENSE_RESULT: request_id[4] + slot[1] + quantity_or_slot[1] + result[1]
```

Older panel-ui documentation describes shorter payloads without the request
ID. Do not implement or archive a conflicting format silently. Before
production integration, both contributors must select one canonical payload,
update this document, update the Pi and MCU implementations, and add matching
vectors. The accepted format is then frozen until an explicitly reviewed
protocol change.

Medication records and schedules remain Pi-owned. The MCU receives commands
and reports hardware outcomes; it is not the medication database.

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
