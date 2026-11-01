# Pi-to-MCU bench checks

These scripts require a real serial device and are not run by the deterministic
unit-test command. From the repository root, provide the Pi source directory
on `PYTHONPATH`:

```bash
PYTHONPATH=pi/src python pi/tests/integration/uart_connection_test.py
```
