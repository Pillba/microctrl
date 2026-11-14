from __future__ import annotations

import argparse
import time

import serial

from protocol.uart_protocol import (
    DISPENSE_CMD,
    DISPENSE_RESULT,
    Frame,
    FrameParser,
    encode_frame,
)


DEFAULT_REQUEST_ID = bytes.fromhex("99 AC A9 BB")
DEFAULT_PORT = "/dev/serial0"
BAUD = 115200
RESPONSE_TIMEOUT_SECONDS = 30.0


def describe(label: str, data: bytes) -> None:
    print(f"{label}: {data.hex(' ').upper()}", flush=True)


def run_test(
    port: str,
    request_id: bytes,
    slot: int,
    quantity: int,
    timeout: float,
) -> int:
    command = Frame(DISPENSE_CMD, request_id + bytes([slot, quantity]))
    encoded_command = encode_frame(command.msg_type, command.payload)
    describe("UART TX", encoded_command)

    parser = FrameParser()
    deadline = time.monotonic() + timeout
    with serial.Serial(
        port=port,
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
        connection.reset_input_buffer()
        connection.reset_output_buffer()
        connection.write(encoded_command)
        connection.flush()

        while time.monotonic() < deadline:
            incoming = connection.read(connection.in_waiting or 1)
            if not incoming:
                continue
            describe("UART RX BYTES", incoming)

            for response in parser.feed(incoming):
                response_raw = encode_frame(response.msg_type, response.payload)
                describe("UART RX FRAME", response_raw)

                if (
                    response.msg_type != DISPENSE_RESULT
                    or len(response.payload) != 7
                    or response.payload[:4] != request_id
                    or response.payload[4] != slot
                ):
                    print("FAIL: unexpected dispense response", flush=True)
                    return 1

                result_code = response.payload[5]
                confirmed_quantity = response.payload[6]
                if result_code == 3:
                    print("BUSY: another dispense is in progress", flush=True)
                    return 2
                if result_code != 0 or confirmed_quantity != quantity:
                    print(
                        f"FAIL: result={result_code}, "
                        f"confirmed_quantity={confirmed_quantity}",
                        flush=True,
                    )
                    return 1

                print("PASS: stepper dispense completed", flush=True)
                return 0

    print(f"FAIL: no response within {timeout:.1f}s", flush=True)
    return 1


def parse_request_id(value: str) -> bytes:
    request_id = bytes.fromhex(value)
    if len(request_id) != 4:
        raise argparse.ArgumentTypeError("request ID must contain exactly 4 bytes")
    return request_id


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Send one framed dispense command to the bench firmware."
    )
    parser.add_argument("--port", default=DEFAULT_PORT)
    parser.add_argument("--slot", type=int, default=1)
    parser.add_argument("--quantity", type=int, default=1)
    parser.add_argument(
        "--request-id",
        type=parse_request_id,
        default=DEFAULT_REQUEST_ID,
        help="four-byte hexadecimal request ID",
    )
    parser.add_argument("--timeout", type=float, default=RESPONSE_TIMEOUT_SECONDS)
    args = parser.parse_args()

    if not 1 <= args.slot <= 6:
        parser.error("--slot must be between 1 and 6")
    if not 1 <= args.quantity <= 255:
        parser.error("--quantity must be between 1 and 255")

    try:
        return run_test(
            args.port,
            args.request_id,
            args.slot,
            args.quantity,
            args.timeout,
        )
    except (OSError, serial.SerialException) as error:
        print(f"UART ERROR: {error}", flush=True)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
