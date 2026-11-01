# MCU firmware

This PlatformIO project is the production firmware boundary. Each subsystem
under `src/subsystems/` owns a header and implementation pair. `src/main.cpp`
only wires those subsystems together.

Build from the repository root:

```bash
pio run -d firmware
```

The current UART module is a compileable boundary, not a completed command
dispatcher. The canonical dispense payload must be approved against
[`../protocol/protocol.md`](../protocol/protocol.md) before behavior is added.
