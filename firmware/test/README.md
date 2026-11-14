# Firmware tests

Host unit tests for the Arduino-free libraries, run with
`pio test -d firmware -e native`:

- `test_uart_protocol/`: frame codec against `protocol/test_vectors/frame_vectors.json`
- `test_command_dispatch/`: what production does with each received frame
- `test_dispense_rules/`: weight tolerance, request validation, result codes

Drivers, pins, timing and actuators are checked on real hardware with the
`bench_console` app, following [`../../docs/testing.md`](../../docs/testing.md).
