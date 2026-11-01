# Microctrl Architecture

## Purpose

This repository contains two cooperating systems:

- `firmware/`: production firmware running on the Arduino ATmega328p.
- `pi/`: Python application running on the Raspberry Pi.

The shared boundary is the serial protocol in `protocol/`. Legacy experiments
are preserved under `archive/` and are not production dependencies.

## Target layout

```text
microctrl/
├── firmware/
│   ├── platformio.ini
│   ├── src/
│   │   ├── main.cpp
│   │   └── subsystems/
│   │       ├── configuration/
│   │       ├── uart_protocol/
│   │       ├── stepper/
│   │       ├── servo/
│   │       ├── pump/
│   │       ├── load_cell/
│   │       ├── ir_break_beam/
│   │       └── hall_sensor/
│   └── test/
├── pi/
│   ├── src/
│   │   ├── main.py
│   │   ├── pages/
│   │   ├── widgets/
│   │   ├── protocol/
│   │   ├── serial_client/
│   │   ├── commands/
│   │   └── db/
│   └── tests/
├── protocol/
│   ├── protocol.md
│   └── test_vectors/
├── docs/
└── archive/
    └── legacy/
```

Each firmware subsystem owns a matching header and implementation file. The
firmware `main.cpp` orchestrates subsystems; it does not contain their driver
logic. Pi code does not control hardware pins directly.

The existing sibling project at
`/Users/Owner/workspace-school/sd1/panel-ui/` is the current Pi application
reference. Its relevant boundaries are:

- `main.py`: PyQt application startup and page wiring.
- `serial_worker.py`: pyserial worker thread, transmit queue, and Qt signals.
- `schema/uart_protocol.py`: frame encoding, CRC-8/MAXIM, and incremental parsing.
- `db.py`: SQLite access for medications, adherence log, and settings.
- `pages/`: manual dispense, scheduling, inventory, and settings workflows.

When the Pi project is moved or reorganized, preserve these responsibilities
but place them under the target `pi/` layout. The existing manual dispense
screen is currently a communication test and does not update inventory; that
must remain explicit until the pending/reserved transaction flow is
implemented.

## Ownership

The Pi contributor owns Python communication code, SQLite, Pi-side commands,
and integration behavior. The firmware contributor owns peripheral drivers,
pin configuration, actuator timing, and MCU command execution.

Both contributors approve changes to the protocol, shared test vectors, or
cross-boundary behavior. Independent Pi or peripheral changes may be reviewed
by the relevant owner.

## Current source of behavior

The existing [`README.md`](../README.md) defines the current pins and initial
UART command schema. Existing sketches are discovery material until behavior
is deliberately promoted into the production firmware and covered by tests.
