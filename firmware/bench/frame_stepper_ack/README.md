# Frame-to-stepper bench

This isolated bench firmware accepts a framed `DISPENSE_CMD` at `115200` baud,
runs the selected stepper, and sends a framed `DISPENSE_RESULT`.

Build and upload from the repository root:

```bash
pio run -d firmware/bench/frame_stepper_ack
pio run -d firmware/bench/frame_stepper_ack -t upload
```

The command payload is:

```text
request_id[4] + slot[1] + quantity[1]
```

For example, `AA 55 10 06 99 AC A9 BB 01 01 32` runs slot 1 for 1,000 steps
and returns a seven-byte result payload containing the request ID, slot, result
code, and confirmed quantity. Result code `0` is success and `3` means busy.
