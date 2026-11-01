from __future__ import annotations

import time

import serial

from protocol.uart_protocol import (
    DISPENSE_CMD,
    DISPENSE_RESULT,
    Frame,
    FrameParser,
    encode_frame,
)


PORT = "/dev/serial0"
BAUD = 115200
REQUEST_ID = bytes.fromhex("99 ac a9 bb")
SLOT = 1
QUANTITY = 1
RESPONSE_TIMEOUT_SECONDS = 3.0
RETRY_DELAY_SECONDS = 0.5


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


def describe_frame(prefix: str, raw: bytes) -> None:
    print(f"{prefix}: {raw.hex(' ').upper()}", flush=True)


def wait_for_response(
    connection: serial.Serial,
    expected: Frame,
) -> bool:
    parser = FrameParser()
    deadline = time.monotonic() + RESPONSE_TIMEOUT_SECONDS

    while time.monotonic() < deadline:
        incoming = connection.read(connection.in_waiting or 1)
        if not incoming:
            continue

        describe_frame("UART RX BYTES", incoming)
        for response in parser.feed(incoming):
            response_raw = encode_frame(response.msg_type, response.payload)
            describe_frame("UART RX FRAME", response_raw)

            if (
                response.msg_type != DISPENSE_RESULT
                or len(response.payload) != 6
                or response.payload[:4] != expected.payload[:4]
                or response.payload[4] != expected.payload[4]
            ):
                print("FAIL: response does not match the request", flush=True)
                return False

            result_code = response.payload[5]
            if result_code != 0:
                print(
                    f"FAIL: MCU returned result code 0x{result_code:02X}",
                    flush=True,
                )
                return False

            print("PASS: MCU response matched and reported success", flush=True)
            return True

    print(
        f"FAIL: no valid MCU response within {RESPONSE_TIMEOUT_SECONDS:.1f}s",
        flush=True,
    )
    return False


def main() -> int:
    command = Frame(DISPENSE_CMD, REQUEST_ID + bytes([SLOT, QUANTITY]))
    encoded_command = encode_frame(command.msg_type, command.payload)

    print(f"Opening {PORT} at {BAUD} 8N1", flush=True)
    try:
        with serial.Serial(
            port=PORT,
            baudrate=BAUD,
            timeout=0.05,
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
            while True:
                connection.reset_input_buffer()
                describe_frame("UART TX", encoded_command)
                connection.write(encoded_command)
                connection.flush()

                wait_for_response(connection, command)
                time.sleep(RETRY_DELAY_SECONDS)
    except KeyboardInterrupt:
        print("Stopped.", flush=True)
        return 0
    except (OSError, serial.SerialException) as error:
        print(f"UART ERROR: {error}", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
