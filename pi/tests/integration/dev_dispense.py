from __future__ import annotations

import argparse
import os
import sys
import uuid
from dataclasses import dataclass
from enum import Enum
import serial
import pygame

from PyQt5.QtCore import QEvent, QObject, QTimer, Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QApplication,
    QFormLayout,
    QLabel,
    QMainWindow,
    QPushButton,
    QSpinBox,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from buttons import ButtonController
from protocol.uart_protocol import (
    DISPENSE_CMD,
    DISPENSE_RESULT,
    decode_frame,
    encode_frame,
)


SEQUENCE_BAUD = 9600
SEQUENCE_TIMEOUT_MS = 30_000
AUDIO_DELAY_MS = 5_000
AUDIO_FILE = "/home/pillba/Downloads/music.mp3"


class DispenseState(Enum):
    READY = "ready"
    DISPENSING = "dispensing"
    RESULT = "result"


@dataclass(frozen=True)
class Medication:
    name: str
    slot: int
    quantity_available: int


class MockUartTransport(QObject):
    """Development transport that sends a framed command and emits a framed reply."""

    frame_received = pyqtSignal(bytes)

    def __init__(self, outcome: str = "success", response_delay_ms: int = 700) -> None:
        super().__init__()
        self.outcome = outcome
        self.response_delay_ms = response_delay_ms
        self.last_command: bytes | None = None

    def send(self, frame: bytes) -> None:
        self.last_command = frame
        if self.outcome == "timeout":
            return

        def respond() -> None:
            # The result echoes the request id and slot so the UI can match it.
            request_id = frame[4:8]
            slot = frame[8]
            result_code = 0 if self.outcome == "success" else 1
            self.frame_received.emit(
                encode_frame(
                    DISPENSE_RESULT,
                    request_id + bytes([slot, result_code]),
                )
            )

        QTimer.singleShot(self.response_delay_ms, respond)


class DispenseController(QObject):
    state_changed = pyqtSignal(object)
    result_changed = pyqtSignal(str)
    dispense_succeeded = pyqtSignal()

    def __init__(
        self,
        transport: MockUartTransport,
        medication: Medication,
        acknowledgement_timeout_ms: int = 2500,
    ) -> None:
        super().__init__()
        self.transport = transport
        self.medication = medication
        self.acknowledgement_timeout_ms = acknowledgement_timeout_ms
        self.state = DispenseState.READY
        self._request_id: bytes | None = None
        self._slot: int | None = None
        self._serial_buffer = bytearray()
        self._timeout_timer = QTimer(self)
        self._timeout_timer.setSingleShot(True)
        self._timeout_timer.timeout.connect(self._on_timeout)
        transport.frame_received.connect(self._on_frame)

    def start(self, slot: int, quantity: int) -> None:
        if self.state is DispenseState.DISPENSING:
            return
        if not 0 <= slot <= 255:
            self._show_result("Failed: servo angle must be between 0 and 180.")
            return
        if quantity < 1 or quantity > self.medication.quantity_available:
            self._show_result("Failed: insufficient development inventory.")
            return

        self._request_id = uuid.uuid4().bytes[:4]
        self._slot = slot
        payload = self._request_id + bytes([slot, quantity])
        self.state = DispenseState.DISPENSING
        self.state_changed.emit(self.state)
        self.result_changed.emit("Sending dispense command over UART...")
        self.transport.send(encode_frame(DISPENSE_CMD, payload))
        self._timeout_timer.start(SEQUENCE_TIMEOUT_MS)

    def _on_frame(self, raw_frame: bytes) -> None:
        try:
            frame = decode_frame(raw_frame)
        except ValueError as error:
            self._show_result(f"Failed: invalid UART acknowledgement ({error}).")
            return
        if frame.msg_type != DISPENSE_RESULT or len(frame.payload) != 6:
            return
        if frame.payload[:4] != self._request_id:
            return
        if frame.payload[4] != self._slot:
            return

        self._timeout_timer.stop()
        if frame.payload[5] == 0:
            self._show_result("Dispense successful. MCU acknowledged the dose.")
            self.dispense_succeeded.emit()
        else:
            self._show_result("Dispense failed. MCU reported a hardware error.")

    def _on_timeout(self) -> None:
        self._show_result("Dispense failed. No MCU acknowledgement was received.")

    def on_transport_error(self, message: str) -> None:
        self._show_result(f"Dispense failed. {message}")

    def _show_result(self, message: str) -> None:
        self._timeout_timer.stop()
        self.state = DispenseState.RESULT
        self.result_changed.emit(message)
        self.state_changed.emit(self.state)


class DevDispenseWindow(QMainWindow):
    def __init__(
        self,
        outcome: str = "success",
        auto_trigger: bool = False,
        medication: Medication | None = None,
        port: str = "/dev/serial0",
    ) -> None:
        super().__init__()
        self.setWindowTitle("Pillba Development Dispense")
        self.setFixedSize(320, 480)
        self.medication = medication or Medication("Lisinopril 10 mg", 3, 8)
        self.transport = SerialUartTransport(port=port, baudrate=9600)
        self.controller = DispenseController(self.transport, self.medication)
        self.audio_file = AUDIO_FILE
        self._audio_timer = QTimer(self)
        self._audio_timer.setSingleShot(True)
        self._audio_timer.timeout.connect(self._play_success_audio)
        self.stack = QStackedWidget()
        self.setCentralWidget(self.stack)
        self._build_ready_page()
        self._build_dispensing_page()
        self._build_result_page()
        self.controller.state_changed.connect(self._show_state)
        self.controller.result_changed.connect(self.status_label.setText)
        self.controller.dispense_succeeded.connect(self._schedule_success_audio)
        self.transport.transport_error.connect(self.controller.on_transport_error)
        self.slot_spin.valueChanged.connect(self._slot_changed)
        self.start_button.clicked.connect(self._start_dispense)
        self.retry_button.clicked.connect(self._reset_for_retry)

        self.buttons = ButtonController(self)
        self.buttons.up_pressed.connect(self._focus_previous)
        self.buttons.down_pressed.connect(self._focus_next)
        self.buttons.left_pressed.connect(self._decrease_focused_value)
        self.buttons.right_pressed.connect(self._increase_focused_value)
        self.buttons.select_pressed.connect(self._activate_focused)
        QApplication.instance().installEventFilter(self)
        self.stack.currentChanged.connect(lambda _: self._focus_first_control())
        self._focus_first_control()

        if auto_trigger:
            QTimer.singleShot(100, self.start_button.click)

    def _build_ready_page(self) -> None:
        page = QWidget()
        layout = QVBoxLayout(page)
        title = QLabel("Time to Dispense")
        title.setStyleSheet("font-size: 22px; font-weight: bold;")
        layout.addWidget(title)
        layout.addWidget(QLabel("Development-only dispense trigger"))
        form = QFormLayout()
        form.addRow("Medication", QLabel(self.medication.name))
        self.slot_spin = QSpinBox()
        self.slot_spin.setRange(0, 180)
        self.slot_spin.setValue(self.medication.slot)
        form.addRow("Slot", self.slot_spin)
        self.quantity_spin = QSpinBox()
        self.quantity_spin.setRange(1, self.medication.quantity_available)
        self.quantity_spin.setValue(1)
        form.addRow("Quantity", self.quantity_spin)
        layout.addLayout(form)
        self.start_button = QPushButton("Dispense")
        layout.addWidget(self.start_button)
        layout.addStretch()
        self.stack.addWidget(page)

    def _build_dispensing_page(self) -> None:
        page = QWidget()
        layout = QVBoxLayout(page)
        title = QLabel("Dispensing")
        title.setStyleSheet("font-size: 22px; font-weight: bold;")
        layout.addWidget(title)
        layout.addWidget(QLabel("The command was sent. Waiting for MCU acknowledgement..."))
        layout.addStretch()
        self.stack.addWidget(page)

    def _build_result_page(self) -> None:
        page = QWidget()
        layout = QVBoxLayout(page)
        title = QLabel("Dispense Result")
        title.setStyleSheet("font-size: 22px; font-weight: bold;")
        layout.addWidget(title)
        self.status_label = QLabel()
        self.status_label.setWordWrap(True)
        layout.addWidget(self.status_label)
        self.retry_button = QPushButton("Try Again")
        layout.addWidget(self.retry_button)
        layout.addStretch()
        self.stack.addWidget(page)

    def _slot_changed(self, slot: int) -> None:
        self.medication = Medication(
            self.medication.name,
            slot,
            self.medication.quantity_available,
        )

    def _start_dispense(self) -> None:
        self.controller.start(self.slot_spin.value(), self.quantity_spin.value())

    def _reset_for_retry(self) -> None:
        self._audio_timer.stop()
        self._show_state(DispenseState.READY)

    def _schedule_success_audio(self) -> None:
        self.status_label.setText(
            "Dispense complete. Playing audio in 5 seconds..."
        )
        self._audio_timer.start(AUDIO_DELAY_MS)

    def _play_success_audio(self) -> None:
        try:
            pygame.mixer.init()
            pygame.mixer.music.load(self.audio_file)
            pygame.mixer.music.play()
        except pygame.error as error:
            self.status_label.setText(
                f"Dispense succeeded, but audio failed: {error}"
            )

    def _focusable_controls(self) -> list[QWidget]:
        return [
            widget
            for widget in self.stack.currentWidget().findChildren(QWidget)
            if widget.focusPolicy() != Qt.NoFocus and widget.isEnabled()
        ]

    def _focus_first_control(self) -> None:
        controls = self._focusable_controls()
        if controls:
            controls[0].setFocus()

    def _focus_previous(self) -> None:
        self.focusPreviousChild()

    def _focus_next(self) -> None:
        self.focusNextChild()

    def _activate_focused(self) -> None:
        focused = QApplication.focusWidget()
        if isinstance(focused, QPushButton):
            focused.click()

    def _decrease_focused_value(self) -> None:
        focused = QApplication.focusWidget()
        if isinstance(focused, QSpinBox):
            focused.stepDown()
        else:
            self._go_back()

    def _increase_focused_value(self) -> None:
        focused = QApplication.focusWidget()
        if isinstance(focused, QSpinBox):
            focused.stepUp()
        else:
            self._focus_next()

    def _go_back(self) -> None:
        if self.controller.state is not DispenseState.DISPENSING:
            self._show_state(DispenseState.READY)

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if event.type() == QEvent.KeyPress:
            key_actions = {
                Qt.Key_Up: self._focus_previous,
                Qt.Key_Down: self._focus_next,
                Qt.Key_Left: self._decrease_focused_value,
                Qt.Key_Right: self._increase_focused_value,
                Qt.Key_Return: self._activate_focused,
                Qt.Key_Enter: self._activate_focused,
                Qt.Key_Space: self._activate_focused,
            }
            action = key_actions.get(event.key())
            if action is not None:
                action()
                return True
        return super().eventFilter(watched, event)

    def _show_state(self, state: DispenseState) -> None:
        self.stack.setCurrentIndex(
            {
                DispenseState.READY: 0,
                DispenseState.DISPENSING: 1,
                DispenseState.RESULT: 2,
            }[state]
        )
        self.start_button.setEnabled(state is DispenseState.READY)

    def closeEvent(self, event) -> None:
        self._audio_timer.stop()
        self.buttons.cleanup()
        self.transport.close()
        if pygame.mixer.get_init():
            pygame.mixer.music.stop()
            pygame.mixer.quit()
        super().closeEvent(event)


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Run the standalone dispense UI prototype.")
    parser.add_argument(
        "--auto-trigger",
        action="store_true",
        help="send the sample dispense command immediately",
    )
    parser.add_argument(
        "--port",
        default="/dev/serial0",
        help="Arduino UART device (default: /dev/serial0)",
    )
    return parser.parse_args(argv)

class SerialUartTransport(QObject):
    """Pi UART transport for the Arduino's SERVO:<angle> line protocol."""

    frame_received = pyqtSignal(bytes)
    transport_error = pyqtSignal(str)

    def __init__(
        self,
        port: str = "/dev/serial0",
        baudrate: int = 9600,
    ) -> None:
        super().__init__()
        self.serial = serial.Serial(
            port=port,
            baudrate=baudrate,
            timeout=1.0,
            write_timeout=1.0,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        self.serial.reset_input_buffer()
        self.serial.reset_output_buffer()
        self._request_id: bytes | None = None
        self._slot: int | None = None
        self._serial_buffer = bytearray()
        self._poll_timer = QTimer(self)
        self._poll_timer.timeout.connect(self._read_serial)
        self._poll_timer.start(20)
    def send(self, frame: bytes) -> None:
        try:
            decoded = decode_frame(frame)
            if decoded.msg_type != DISPENSE_CMD or len(decoded.payload) != 6:
                raise ValueError("invalid dispense command")

            self._request_id = decoded.payload[:4]
            self._slot = decoded.payload[4]
            angle = self._slot
            if not 0 <= angle <= 180:
                raise ValueError("servo angle must be between 0 and 180")

            self.serial.reset_input_buffer()
            self.serial.write(b"G")
            self.serial.flush()
        except (OSError, ValueError, serial.SerialException) as error:
            self.transport_error.emit(f"UART send failed: {error}")

    def _read_serial(self) -> None:
        try:
            if self.serial.in_waiting == 0:
                return
            self._serial_buffer.extend(self.serial.read(self.serial.in_waiting))
            while b"\n" in self._serial_buffer:
                line, _, remainder = self._serial_buffer.partition(b"\n")
                self._serial_buffer = bytearray(remainder)
                response = line.decode("utf-8", errors="replace").strip()
                if response:
                    self._handle_response(response)
        except (OSError, serial.SerialException) as error:
            self.transport_error.emit(f"UART receive failed: {error}")

    def _handle_response(self, response: str) -> None:
        if self._request_id is None or self._slot is None:
            return

        normalized = response.lower()
        if normalized == "done":
            result_code = 0
        elif normalized.startswith("error"):
            result_code = 1
        else:
            return
        self.frame_received.emit(
            encode_frame(
                DISPENSE_RESULT,
                self._request_id + bytes([self._slot, result_code]),
            )
        )

    def close(self) -> None:
        self._poll_timer.stop()
        if self.serial.is_open:
            self.serial.close()

def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    lcd_enabled = os.environ.get("PILLBA_LCD") == "1"
    if lcd_enabled:
        os.environ["QT_QPA_PLATFORM"] = "offscreen"

    app = QApplication(sys.argv)
    try:
        window = DevDispenseWindow(auto_trigger=args.auto_trigger, port=args.port)
    except (OSError, serial.SerialException) as error:
        print(f"Unable to open Arduino UART {args.port}: {error}", file=sys.stderr)
        return 1
    window.show()

    if lcd_enabled:
        from lcd_display import ST7796Display, qimage_to_pil

        lcd = ST7796Display()

        def refresh_lcd() -> None:
            app.processEvents()
            lcd.show(qimage_to_pil(window.grab().toImage()))

        refresh_timer = QTimer(window)
        refresh_timer.timeout.connect(refresh_lcd)
        refresh_timer.start(200)
        refresh_lcd()

    return app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
