from __future__ import annotations

import uuid

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFormLayout,
    QLabel,
    QPushButton,
    QSpinBox,
    QVBoxLayout,
    QWidget,
)

from db import Medication, list_medications
from serial_worker import SerialWorker
from widgets.card import CardWidget, WarningBanner
from widgets.top_bar import TopBar


class DispenseWidget(QWidget):
    go_back = pyqtSignal()

    def __init__(self, worker: SerialWorker | None = None, parent=None) -> None:
        super().__init__(parent)
        self.worker = worker
        self._request_in_flight = False
        self._medications: list[Medication] = []

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        top_bar = TopBar("Manual Dispense")
        top_bar.back_clicked.connect(self.go_back.emit)
        layout.addWidget(top_bar)

        body = QWidget()
        body_layout = QVBoxLayout(body)
        body_layout.setContentsMargins(12, 12, 12, 12)
        body_layout.setSpacing(10)

        form_card = CardWidget()
        form = QFormLayout(form_card)
        form.setContentsMargins(12, 12, 12, 12)
        form.setSpacing(10)

        self.medication_selector = QComboBox()
        self.medication_selector.currentIndexChanged.connect(self._medication_changed)
        form.addRow("Medication", self.medication_selector)

        self.quantity = QSpinBox()
        self.quantity.setRange(1, 1)
        self.quantity.valueChanged.connect(self._update_button_state)
        form.addRow("Quantity", self.quantity)

        self.override = QCheckBox("Confirm manual override")
        self.override.toggled.connect(self._update_button_state)
        form.addRow("", self.override)
        body_layout.addWidget(form_card)

        warning = WarningBanner()
        warning_layout = QVBoxLayout(warning)
        warning_layout.setContentsMargins(12, 10, 12, 10)
        warning_layout.addWidget(
            QLabel("Communication test only. Inventory is not changed by an echo.")
        )
        body_layout.addWidget(warning)

        self.status_label = QLabel("Select a medication and confirm the override.")
        self.status_label.setWordWrap(True)
        body_layout.addWidget(self.status_label)

        self.dispense_button = QPushButton("Send Dispense Command")
        self.dispense_button.setProperty("class", "primary")
        self.dispense_button.clicked.connect(self._send_command)
        body_layout.addWidget(self.dispense_button)
        body_layout.addStretch(1)
        layout.addWidget(body)

        self._load_medications()
        if worker is not None:
            self.set_worker(worker)

    def set_worker(self, worker: SerialWorker) -> None:
        self.worker = worker
        worker.connected.connect(lambda: self._set_status("UART connected at 115200 baud."))
        worker.connection_error.connect(self._on_worker_error)
        worker.transport_error.connect(self._on_worker_error)
        worker.echo_result.connect(self._on_echo_result)
        self._update_button_state()

    def _load_medications(self) -> None:
        self._medications = [item for item in list_medications() if item.active]
        self.medication_selector.clear()
        for medication in self._medications:
            strength = f" {medication.strength}" if medication.strength else ""
            self.medication_selector.addItem(
                f"{medication.name}{strength} · Slot {medication.slot} · "
                f"{medication.remaining_quantity} left"
            )
        self._medication_changed(self.medication_selector.currentIndex())

    def _medication_changed(self, index: int) -> None:
        if 0 <= index < len(self._medications):
            available = self._medications[index].remaining_quantity
            self.quantity.setRange(1, max(1, available))
            self.quantity.setValue(min(self.quantity.value(), max(1, available)))
            self.quantity.setEnabled(available > 0)
        else:
            self.quantity.setRange(1, 1)
            self.quantity.setEnabled(False)
        self._update_button_state()

    def _update_button_state(self) -> None:
        index = self.medication_selector.currentIndex()
        medication_available = (
            0 <= index < len(self._medications)
            and self._medications[index].remaining_quantity > 0
        )
        self.dispense_button.setEnabled(
            medication_available
            and self.override.isChecked()
            and self.worker is not None
            and not self._request_in_flight
        )

    def _send_command(self) -> None:
        if self.worker is None or not self._medications:
            self._set_status("UART is not available.")
            return
        medication = self._medications[self.medication_selector.currentIndex()]
        if self.quantity.value() > medication.remaining_quantity:
            self._set_status("Quantity exceeds remaining inventory.")
            return
        request_id = uuid.uuid4().bytes[:4]
        self._request_in_flight = True
        self._update_button_state()
        self._set_status(
            f"Sending slot {medication.slot}, quantity {self.quantity.value()}..."
        )
        try:
            self.worker.send_dispense(
                request_id,
                medication.slot,
                self.quantity.value(),
            )
        except ValueError as error:
            self._request_in_flight = False
            self._update_button_state()
            self._set_status(f"Invalid command: {error}")

    def _on_echo_result(self, success: bool, message: str) -> None:
        self._request_in_flight = False
        self._update_button_state()
        self._set_status(message)

    def _on_worker_error(self, message: str) -> None:
        self._request_in_flight = False
        self._update_button_state()
        self._set_status(message)

    def _set_status(self, message: str) -> None:
        self.status_label.setText(message)
