from __future__ import annotations

import sqlite3

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import QComboBox, QLabel, QPushButton, QVBoxLayout, QWidget

from db import list_medications, refill_medication
from widgets.card import CardWidget
from widgets.top_bar import TopBar


class RefillInventoryWidget(QWidget):
    go_back = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        top = TopBar("Refill Medication")
        top.back_clicked.connect(self.go_back.emit)
        layout.addWidget(top)
        card = CardWidget()
        form = QVBoxLayout(card)
        form.setContentsMargins(12, 12, 12, 12)
        self.selector = QComboBox()
        for medication in list_medications():
            self.selector.addItem(f"Slot {medication.slot}: {medication.name}", medication.slot)
        form.addWidget(QLabel("Select a medication to refill to full capacity."))
        form.addWidget(self.selector)
        self.message = QLabel()
        form.addWidget(self.message)
        button = QPushButton("Refill Medication")
        button.clicked.connect(self._refill)
        form.addWidget(button)
        layout.addWidget(card)
        layout.addStretch(1)

    def _refill(self) -> None:
        slot = self.selector.currentData()
        if slot is None:
            self.message.setText("No medication is configured.")
            return
        try:
            refill_medication(int(slot))
        except sqlite3.Error as error:
            self.message.setText(str(error))
            return
        self.go_back.emit()
