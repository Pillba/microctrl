# MCU firmware

One PlatformIO project for the ATmega328P Pillba PCB. The drivers live once in
`lib/`; each app in `src/apps/` wires them together for one purpose.

```text
firmware/
├── platformio.ini            envs: production, bench_console, native
├── lib/
│   ├── board/                pin map, timings, EEPROM layout (board.h)
│   ├── stepper_bank/         six DRV8805 drivers on shared STEP/DIR, nENBL select
│   ├── shield/               servo + two hall sensors (not fitted yet), move-and-confirm
│   ├── ir_beam/              pin-change break counter, clear level learned at boot
│   ├── scale/                HX711 + EEPROM calibration, steady reading
│   ├── pump/                 pump on/off
│   ├── dispense/             DispenseSequence: pills (shield, tare, step, IR, weight, open)
│   │                         and liquid (shield, tare, pump to weight, weight, open)
│   ├── dispense_rules/       pure: result codes, weight tolerances, liquid limits, request checks
│   └── protocol/             pure: CRC-8/MAXIM, frame parser/encoder, dispatch decision
├── src/apps/
│   ├── production/           framed UART, 115200 (protocol/protocol.md): dispense, scale calibration
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

## Keep the calibration across flashes: program EESAVE once per board

The scale calibration factor lives in EEPROM (`lib/board/board.h`, EEPROM
layout) and is shared by `production`, `bench_console` and
`bench/dispense_UI`. Every upload runs a chip erase, which also wipes EEPROM
unless the EESAVE fuse is programmed. `platformio.ini` sets
`board_fuses.hfuse = 0xD1` (the old `0xD9` with only EESAVE, bit 3, changed).
Program it once per board, then upload again, because programming the fuses
also erases the chip:

```bash
pio run -d firmware -e production -t fuses --upload-port /dev/cu.usbserial-XXXX
pio run -d firmware -e production -t upload --upload-port /dev/cu.usbserial-XXXX
```

Check it: calibrate, flash again, and the `bench_console` setup line says
"Scale factor loaded from this board" (not "NOT calibrated").

## Order for a new board

1. Program the fuses (above).
2. Calibrate the scale, any one of:
   - `production`: the panel's Settings → Calibrate Scale page, or
     `bench_menu_frames.py` → `c` (both send CALIBRATE_START / FINISH);
   - `bench_console`: `c` at 9600 baud.
3. Flash `production` (if it is not already on the board). The calibration stays.

## Hall sensors (not fitted)

The two hall sensors that confirm the shield position are not wired yet.
`board::HALL_SENSORS_FITTED = false` keeps their code in place but unused: a
shield move commands the servo, waits `SHIELD_TRAVEL_MS` (700 ms) and reports
"not confirmed" (bench_console prints a NOTE). To fit them: wire HALL1 (closed)
to pin 3 and HALL2 (open) to pin 4, set the switch to `true`, then check the
`HALL:` lines with `bench_console` `8` / `9` before flashing production.

## Liquid (slot 7)

`test 7 [ml]` in `bench_console` (or `test 7 [ml]` in `bench_menu_frames.py`
against production) runs the pump until the cup gains `ml × 1.00 g`. The
limits are in `lib/dispense_rules/dispense_rules.h` and are first guesses to
tune on the bench:

- measure the flow: cup on the scale, `t`, `pon`, wait 10 s, `poff`, `w`;
  grams / 10 = mL/s. `LIQUID_MS_PER_ML = 2000` assumes at least 0.5 mL/s. If
  the pump is slower, raise it **and** the Pi's liquid timeout
  (protocol/protocol.md, "Timing");
- `LIQUID_TOLERANCE_PCT` / `LIQUID_TOLERANCE_MIN_G` must cover the overshoot
  you see in `test 7` (the `PUMPED` line shows the weight when the pump stopped);
- `LIQUID_MAX_ML = 30` is the cup's safe volume: set it to the real cup and
  mirror it in the Pi.
