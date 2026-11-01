from __future__ import annotations

from PyQt5.QtCore import pyqtSignal
from PyQt5.QtWidgets import QVBoxLayout, QWidget

from widgets.nav_card import NavCardButton
from widgets.top_bar import TopBar


class InventoryActionsWidget(QWidget):
    go_back = pyqtSignal()
    add_requested = pyqtSignal()
    remove_requested = pyqtSignal()
    refill_requested = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        top_bar = TopBar("Edit Inventory")
        top_bar.back_clicked.connect(self.go_back.emit)
        layout.addWidget(top_bar)

        actions = QVBoxLayout()
        actions.setContentsMargins(10, 12, 10, 12)
        actions.setSpacing(10)
        for title, subtitle, color, signal in (
            ("Add Medication", "Create a new canister", "#147D77", self.add_requested),
            ("Remove Medication", "Remove a canister", "#C0392B", self.remove_requested),
            ("Refill Medication", "Restore a canister to full", "#2161F5", self.refill_requested),
        ):
            button = NavCardButton(title, subtitle, color, 0)
            button.clicked.connect(lambda checked=False, target=signal: target.emit())
            actions.addWidget(button)
        actions.addStretch(1)
        layout.addLayout(actions, 1)
