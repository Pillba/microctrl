# Pi tests

Run deterministic tests from the repository root with:

```bash
python -m unittest discover -s pi/tests
```

Place real Pi-to-MCU bench checks under `integration/`. They require connected
hardware and are not substitutes for the deterministic protocol tests.

To send one dispense command to the bench firmware after uploading it:

```bash
PYTHONPATH=pi/src python pi/tests/integration/bench_dispense_frame_test.py \
  --port /dev/cu.usbserial-1140
```

The script sends one framed command for slot 1 and quantity 1, then waits for
the framed seven-byte dispense result. Override the slot, quantity, request ID,
or port with the command-line options.

To drive the whole `dispense.cpp` bench menu (`test N`, `1`–`6`, `stop`, `8`/`9`,
`pon`/`poff`, `t`, `r`, `w`, `c`, `e`) over frames against
`firmware/bench/dispense_UI`:

```bash
PYTHONPATH=pi/src python pi/tests/integration/bench_menu_frames.py --port /dev/cu.usbserial-1140
PYTHONPATH=pi/src python pi/tests/integration/bench_menu_frames.py --port /dev/serial0 -c 8 -c "test 1" -c w
```

Without `-c` the menu is interactive; with `-c` it runs the commands in order
and exits nonzero if any fails. `test_bench_menu_frames.py` covers its frame
mapping without hardware.

