# Testing and Hardware Validation

Tests are organized by boundary and ownership.

```text
firmware/test/
├── test_uart_protocol/
├── test_command_dispatch/
└── test_subsystems/

pi/tests/
├── protocol/
├── communication/
├── commands/
└── db/

protocol/test_vectors/
```

## Required layers

- Pi unit tests for encoding, decoding, serial state, commands, and SQLite.
- Firmware unit tests for protocol parsing, dispatch, and non-hardware logic.
- Shared contract tests using identical byte-level vectors on both sides.
- Pi-to-MCU integration tests on a real Pi and MCU bench.
- Physical hardware validation for pins, timing, actuators, and sensors.

Hardware-only behavior is not faked as proof of correctness. Logic that can be
isolated should be tested automatically, while actual driver, pin, timing,
load-cell, and actuator behavior requires physical validation.

## Boundary expectations

Pi tests use a fake or controlled serial boundary where practical. They verify
that a command is encoded correctly and that success, error, timeout,
disconnect, malformed response, and unknown request IDs are handled correctly.

Firmware tests verify that valid frames dispatch to the correct subsystem,
invalid frames are rejected, and command validation is enforced. Peripheral
tests verify safe startup and error states on real hardware.

Every bug fix adds a regression test at the boundary where the bug occurred.
Tests must not depend on archived sketches or on generated `.pio/` output.

The current panel-ui project provides useful Pi-side starting points:

- `pi/src/protocol/uart_protocol.py` for frame encode/decode and parser cases;
- `serial_worker.py` for connection, queue, and response-routing cases;
- `uart_connection_test.py` for a real UART smoke test;
- `db.py` and `create_examples.sql` for SQLite fixtures and invariants;
- `pages/dispense.py` for manual-dispense workflow cases.

The real-UART script is a bench smoke test, not a substitute for deterministic
unit tests. In particular, its current echo-oriented dispense flow must be
extended to cover the `UNKNOWN` outcome and pending inventory transaction
before it is treated as production validation.

## Bench evidence

Record the board, firmware revision, protocol revision or commit, wiring,
command sent, observed response, and pass/fail result for physical tests.
Hardware validation is part of the definition of done for hardware changes.
