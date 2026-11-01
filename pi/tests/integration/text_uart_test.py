from __future__ import annotations

import time

import serial


PORT = "/dev/ttyS0"
BAUD = 9600
MESSAGE = "hello from pi"
RESPONSE_TIMEOUT_SECONDS = 3.0


def drain_input(connection: serial.Serial) -> None:
    """Discard startup and stale input until the UART stays quiet briefly."""
    quiet_deadline = time.monotonic() + 0.2

    while time.monotonic() < quiet_deadline:
        waiting = connection.in_waiting
        if waiting:
            connection.read(waiting)
            quiet_deadline = time.monotonic() + 0.05
        else:
            time.sleep(0.01)


def main() -> int:
    outgoing = f"{MESSAGE}\n".encode("ascii")
    print(f"Opening {PORT} at {BAUD} 8N1", flush=True)
    print(f"UART TX TEXT: {outgoing!r}", flush=True)
    try:
        with serial.Serial(
            port=PORT,
            baudrate=BAUD,
            timeout=0.15,
            write_timeout=1.0,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        ) as connection:
            drain_input(connection)
            connection.reset_input_buffer()
            connection.reset_output_buffer()
            connection.write(outgoing)
            connection.flush()

            response = bytearray()
            deadline = time.monotonic() + RESPONSE_TIMEOUT_SECONDS
            while time.monotonic() < deadline:
                response.extend(connection.read(connection.in_waiting or 1))
                if response.endswith(b"\n"):
                    break

            if not response:
                print("FAIL: no MCU response", flush=True)
                return 1

            print(f"UART RX BYTES: {bytes(response)!r}", flush=True)
            text = bytes(response).decode("ascii", errors="replace").rstrip("\r\n")
            print(f"UART RX TEXT: {text}", flush=True)

            expected = f"{MESSAGE} from mcu"
            if text != expected:
                print(f"FAIL: expected {expected!r}", flush=True)
                return 1

            print("PASS: MCU text echo matched", flush=True)
            return 0
    except (OSError, serial.SerialException) as error:
        print(f"UART ERROR: {error}", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
