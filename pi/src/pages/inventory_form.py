from __future__ import annotations

import sqlite3

from PyQt5.QtCore import QTime, pyqtSignal
from PyQt5.QtWidgets import (
    QFormLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QScrollArea,
    QSpinBox,
    QTimeEdit,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from db import Medication, list_medications, upsert_medication
from widgets.card import CardWidget
from widgets.top_bar import TopBar


class AddInventoryWidget(QWidget):
    go_back = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._build()

    def _build(self) -> None:
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        top_bar = TopBar("Add Medication")
        top_bar.back_clicked.connect(self.go_back.emit)
        layout.addWidget(top_bar)
        body = QWidget()
        body.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        form_card = CardWidget()
        form = QFormLayout(form_card)
        form.setContentsMargins(12, 12, 12, 12)
        self.name = QLineEdit()
        self.name.setPlaceholderText("Medication name")
        self.strength = QLineEdit()
        self.strength.setPlaceholderText("Strength (optional)")
        self.slot = QSpinBox()
        self.slot.setRange(1, 8)
        self.capacity = QSpinBox()
        self.capacity.setRange(1, 10000)
        self.capacity.setValue(30)
        self.frequency = QSpinBox()
        self.frequency.setRange(0, 168)
        self.frequency.setSpecialValueText("As needed")
        self.dose_time = QTimeEdit(QTime(8, 0))
        self.dose_time.setDisplayFormat("HH:mm")
        form.addRow("Name", self.name)
        form.addRow("Strength", self.strength)
        form.addRow("Slot", self.slot)
        form.addRow("Capacity", self.capacity)
        form.addRow("Every (hours)", self.frequency)
        form.addRow("First dose", self.dose_time)
        self.message = QLabel()
        self.save = QPushButton("Add Medication")
        self.save.clicked.connect(self._save)
        body_layout = QVBoxLayout(body)
        body_layout.setContentsMargins(12, 12, 12, 12)
        body_layout.addWidget(form_card)
        body_layout.addWidget(self.message)
        body_layout.addWidget(self.save)
        body_layout.addStretch(1)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(body)
        layout.addWidget(scroll, 1)

    def _save(self) -> None:
        name = self.name.text().strip()
        if not name:
            self.message.setText("Enter a medication name.")
            return
        try:
            upsert_medication(
                Medication(
                    None,
                    name,
                    self.strength.text().strip() or None,
                    self.slot.value(),
                    frequency_hours=self.frequency.value() or None,
                    dose_time=self.dose_time.time().toString("HH:mm"),
                    total_quantity=self.capacity.value(),
                    remaining_quantity=self.capacity.value(),
                )
            )
        except (ValueError, sqlite3.IntegrityError) as error:
            self.message.setText(str(error))
            return
        self.go_back.emit()


class EditScheduleWidget(QWidget):
    go_back = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        top = TopBar("Edit Schedule")
        top.back_clicked.connect(self.go_back.emit)
        layout.addWidget(top)
        body = QWidget()
        body.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        body_layout = QVBoxLayout(body)
        body_layout.setContentsMargins(12, 12, 12, 12)
        body_layout.setSpacing(10)
        self.message = QLabel()
        self.rows = QVBoxLayout()
        self.rows.setSpacing(10)
        body_layout.addLayout(self.rows)
        body_layout.addWidget(self.message)
        body_layout.addStretch(1)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(body)
        layout.addWidget(scroll, 1)
        self._populate()

    def _populate(self) -> None:
        for medication in list_medications():
            card = CardWidget()
            form = QFormLayout(card)
            form.setContentsMargins(12, 10, 12, 10)
            frequency = QSpinBox()
            frequency.setRange(0, 168)
            frequency.setSpecialValueText("As needed")
            frequency.setValue(medication.frequency_hours or 0)
            dose_time = QTimeEdit(QTime.fromString(medication.dose_time, "HH:mm"))
            dose_time.setDisplayFormat("HH:mm")
            save = QPushButton("Save")
            save.clicked.connect(
                lambda checked=False, item=medication, field=frequency, time_field=dose_time:
                self._save(item, field.value() or None, time_field)
            )
            form.addRow(QLabel(medication.name), frequency)
            form.addRow("First dose", dose_time)
            form.addRow("", save)
            self.rows.addWidget(card)

    def _save(self, medication: Medication, frequency: int | None, dose_time: QTimeEdit) -> None:
        try:
            upsert_medication(
                Medication(
                    **{
                        **medication.__dict__,
                        "frequency_hours": frequency,
                        "dose_time": dose_time.time().toString("HH:mm"),
                    }
                )
            )
            self.message.setText("Schedule updated.")
        except ValueError as error:
            self.message.setText(str(error))
