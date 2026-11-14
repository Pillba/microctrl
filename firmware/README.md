# MCU firmware

One PlatformIO project for the ATmega328P Pillba PCB. The drivers live once in
`lib/`; each app in `src/apps/` wires them together for one purpose.

```text
firmware/
├── platformio.ini            envs: production, bench_console, native
├── lib/
│   ├── board/                pin map, timings, EEPROM layout (board.h)
│   ├── stepper_bank/         six DRV8805 drivers on shared STEP/DIR, nENBL select
│   ├── shield/               servo + two hall sensors, debounced, move-and-confirm
│   ├── ir_beam/              pin-change break counter, clear level learned at boot
│   ├── scale/                HX711 + EEPROM calibration, steady reading
│   ├── pump/                 pump on/off
│   ├── dispense/             DispenseSequence: shield, tare, step, IR, weight, open
│   ├── dispense_rules/       pure: result codes, weight tolerance, request checks
│   └── protocol/             pure: CRC-8/MAXIM, frame parser/encoder, dispatch decision
├── src/apps/
│   ├── production/           framed UART, 115200 (protocol/protocol.md)
│   └── bench_console/        plain-text commands, 9600 (same as bench/dispense)
├── test/                     host unit tests for the pure libraries
└── bench/                    frozen standalone sketches used as references
```

## Rules

- Drivers never touch `Serial`. Only apps print or send frames. Progress of a
  dispense reaches the app through a `DispenseListener` callback.
- No virtual functions and no heap: the ATmega328P has 2 KB of RAM. Each
  driver has exactly one static instance in the app's `main.cpp`.
- Long blocking operations accept a `PollHook` so the app keeps servicing the
  UART (and, in production, answers `BUSY` immediately).
- `protocol` and `dispense_rules` must stay free of `Arduino.h` so they build
  for the `native` test env.
- `bench/dispense/` is the hardware-validated reference. Change a pin or timing
  in `lib/board/board.h` only after re-validating it on the bench.

## Build, test, upload

```bash
pio run  -d firmware                       # production (default env)
pio run  -d firmware -e bench_console
pio test -d firmware -e native             # protocol, dispatch, dispense rules
pio run  -d firmware -e production -t upload --upload-port /dev/cu.usbserial-XXXX
```

To avoid passing `--upload-port`, copy `platformio_override.ini.example` to
`platformio_override.ini` (gitignored) and set your port there.

The scale calibration factor lives in EEPROM and is shared by both apps:
calibrate once with `bench_console` (`c`), then flash `production`.
