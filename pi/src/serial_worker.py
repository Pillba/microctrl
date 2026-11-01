from __future__ import annotations

import os
import queue
from dataclasses import dataclass

import serial
from PyQt5.QtCore import QThread, pyqtSignal

from protocol.uart_protocol import (
    DISPENSE_CMD,
    DISPENSE_RESULT,
    Frame,
    FrameParser,
    encode_frame,
)


@dataclass(frozen=True)
class DeviceStatus:
    battery_pct: int = 100
    wifi_rssi: int = -42
    compartments_filled: int = 0
    error_flags: int = 0


class SerialWorker(QThread):
    connected = pyqtSignal()
    connection_error = pyqtSignal(str)
    transport_error = pyqtSignal(str)
    frame_received = pyqtSignal(object)
    echo_result = pyqtSignal(bool, str)
    device_status = pyqtSignal(object)

    def __init__(self, port: str | None = None, baud: int = 115200) -> None:
        super().__init__()
        self.port = port or os.environ.get("PILLBA_UART_PORT", "/dev/serial0")
        self.baud = baud
        self._commands: queue.Queue[tuple[Frame, bytes] | None] = queue.Queue()
        self._active_frame: Frame | None = None
        self._stop_requested = False

    def send_dispense(self, request_id: bytes, slot: int, quantity: int) -> bytes:
        if len(request_id) != 4:
            raise ValueError("request_id must contain exactly four bytes")
        if not 0 <= slot <= 255:
            raise ValueError("slot must be between 0 and 255")
        if not 1 <= quantity <= 255:
            raise ValueError("quantity must be between 1 and 255")
        command = Frame(DISPENSE_CMD, request_id + bytes([slot, quantity]))
        encoded = encode_frame(command.msg_type, command.payload)
        self._commands.put((command, encoded))
        return encoded

    def stop(self) -> None:
        self._stop_requested = True
        self._commands.put(None)
        if self.isRunning():
            self.wait(2000)

    def run(self) -> None:
        parser = FrameParser()
        try:
            connection = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                timeout=0.05,
                write_timeout=1.0,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
            )
        except (OSError, serial.SerialException) as error:
            self.connection_error.emit(f"Unable to open UART {self.port}: {error}")
            return

        self.connected.emit()
        try:
            while not self._stop_requested:
                self._write_pending(connection)
                incoming = connection.read(connection.in_waiting or 1)
                if incoming:
                    for frame in parser.feed(incoming):
                        self.frame_received.emit(frame)
                        self._handle_frame(frame)
        except (OSError, serial.SerialException) as error:
            self.transport_error.emit(f"UART communication failed: {error}")
        finally:
            connection.close()

    def _write_pending(self, connection: serial.Serial) -> None:
        try:
            frame = self._commands.get_nowait()
        except queue.Empty:
            return
        if frame is None:
            return
        command, encoded = frame
        try:
            self._active_frame = command
            print(f"UART TX: {encoded.hex(' ')}", flush=True)
            connection.write(encoded)
            connection.flush()
        except (OSError, serial.SerialException) as error:
            self.transport_error.emit(f"UART send failed: {error}")

    def _handle_frame(self, response: Frame) -> None:
        print(
            f"UART RX: type=0x{response.msg_type:02x} "
            f"payload={response.payload.hex(' ')}",
            flush=True,
        )
        if self._active_frame is None:
            return
        if response == self._active_frame:
            self.echo_result.emit(True, "PASS: Arduino echoed the frame exactly.")
            print("UART ECHO: PASS", flush=True)
            self._active_frame = None
            return

        expected_request = self._active_frame.payload[:4]
        expected_slot = self._active_frame.payload[4]
        if (
            response.msg_type != DISPENSE_RESULT
            or len(response.payload) != 6
            or response.payload[:4] != expected_request
            or response.payload[4] != expected_slot
        ):
            self.echo_result.emit(
                False,
                "FAIL: Arduino response does not match the dispense request.",
            )
            print("UART ECHO: FAIL (request mismatch)", flush=True)
            self._active_frame = None
            return

        result_code = response.payload[5]
        success = result_code == 0
        message = (
            "PASS: Arduino completed the dispense command."
            if success
            else f"FAIL: Arduino reported result code {result_code}."
        )
        self.echo_result.emit(success, message)
        print(f"UART DISPENSE RESULT: {'PASS' if success else 'FAIL'}", flush=True)
        self._active_frame = None
