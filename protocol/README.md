# Shared protocol

[`protocol.md`](protocol.md) is the authoritative Pi–MCU wire contract.
[`test_vectors/`](test_vectors/) contains byte-level examples that both
implementations must satisfy: `pi/tests/test_uart_protocol.py` and
`firmware/test/test_uart_protocol/` both read `frame_vectors.json`.

The framed dispense payload is implemented by `pi/src/serial_worker.py` and the
firmware `production` app (`firmware/lib/protocol/`): commands contain a
four-byte request ID, slot, and quantity; results contain the request ID, slot,
result code, and confirmed quantity. The firmware `bench_console` app is a
separate plain-text 9600-baud bench console.
