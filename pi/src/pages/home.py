from __future__ import annotations

from datetime import datetime, timedelta

from PyQt5.QtCore import QTimer, pyqtSignal
from PyQt5.QtCore import Qt
from PyQt5.QtWidgets import QHBoxLayout, QLabel, QVBoxLayout, QWidget

from db import list_medications
from widgets.nav_card import NavCardButton


class HomeWidget(QWidget):
    navigate = pyqtSignal(int)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self.header = QWidget()
        self.header.setFixedHeight(140)
        self.header.setStyleSheet("background: #0F172A;")
        header_layout = QHBoxLayout(self.header)
        header_layout.setContentsMargins(20, 16, 20, 16)
        header_layout.setSpacing(10)

        left_column = QVBoxLayout()
        left_column.setSpacing(2)
        left_column.setAlignment(Qt.AlignTop | Qt.AlignLeft)
        pillba_label = QLabel("PILLBA")
        pillba_label.setProperty("class", "micro-label")
        pillba_label.setStyleSheet(
            "color: rgba(248, 250, 252, 0.65); letter-spacing: 3px; font-size: 14px; font-weight: 700;"
        )
        self.clock_label = QLabel("--:--")
        self.clock_label.setProperty("class", "clock")
        self.clock_label.setStyleSheet("font-size: 38px; font-weight: 700; color: #FFFFFF;")
        self.date_label = QLabel("--")
        self.date_label.setProperty("class", "caption")
        self.date_label.setStyleSheet(
            "color: rgba(248, 250, 252, 0.7); font-size: 15px;"
        )
        left_column.addWidget(pillba_label)
        left_column.addWidget(self.clock_label)
        left_column.addWidget(self.date_label)
        self.countdown_label = QLabel()
        self.countdown_label.setProperty("class", "clock")

        right_column = QVBoxLayout()
        right_column.setAlignment(Qt.AlignRight | Qt.AlignTop)
        right_column.setSpacing(4)
        self.online_badge = QLabel("● Online")
        self.online_badge.setStyleSheet(
            "background: rgba(13, 148, 136, 0.2); color: #14B8A6; border-radius: 10px; padding: 3px 8px; font-size: 11px; font-weight: 700;"
        )
        self.dose_name_label = QLabel()
        self.dose_name_label.setProperty("class", "caption")
        self.dose_name_label.setStyleSheet("color: rgba(248, 250, 252, 0.7);")
        right_column.addWidget(self.online_badge, 0, Qt.AlignRight)
        right_column.addWidget(self.countdown_label, 0, Qt.AlignRight)
        right_column.addWidget(self.dose_name_label, 0, Qt.AlignRight)

        header_layout.addLayout(left_column, 1)
        header_layout.addLayout(right_column)

        nav_layout = QVBoxLayout()
        nav_layout.setContentsMargins(16, 14, 16, 14)
        nav_layout.setSpacing(12)

        nav_cards = [
            ("View Schedule", "Upcoming doses", "#147D77", 1),
            ("Manual Dispense", "Override & dispense", "#C0392B", 2),
            ("Medication Inventory", "Remaining volume", "#2161F5", 3),
            ("Device Settings", "Configure device", "#4B5D76", 4),
        ]
        for title, subtitle, color, target in nav_cards:
            card = NavCardButton(title, subtitle, color, target)
            card.clicked.connect(lambda checked=False, value=target: self.navigate.emit(value))
            nav_layout.addWidget(card, 1)

        self._timer = QTimer(self)
        self._timer.timeout.connect(self._update_clock)
        self._timer.start(1000)
        self._update_clock()

        layout.addWidget(self.header)
        layout.addLayout(nav_layout, 1)

    def _update_clock(self) -> None:
        current = datetime.now()
        next_dose = self._next_dose(current)
        if next_dose is None:
            self.countdown_label.clear()
            self.dose_name_label.clear()
        else:
            medication, dose_at = next_dose
            remaining = max(0, int((dose_at - current).total_seconds()))
            minutes, seconds = divmod(remaining, 60)
            self.countdown_label.setText(f"DOSE IN {minutes:02d}:{seconds:02d}")
            self.dose_name_label.setText(medication.name)
        self.clock_label.setText(current.strftime("%I:%M %p"))
        self.date_label.setText(current.strftime("%a, %b %-d"))

    @staticmethod
    def _next_dose(current: datetime):
        window_end = current + timedelta(minutes=30)
        candidates = []
        for medication in list_medications():
            if not medication.active or not medication.frequency_hours:
                continue
            try:
                hour, minute = (int(value) for value in medication.dose_time.split(":"))
                dose_at = current.replace(hour=hour, minute=minute, second=0, microsecond=0)
            except (AttributeError, TypeError, ValueError):
                continue
            while dose_at < current:
                dose_at += timedelta(hours=medication.frequency_hours)
            if current <= dose_at <= window_end:
                candidates.append((medication, dose_at))
        return min(candidates, key=lambda item: item[1]) if candidates else None

    def on_device_status(self, status: object) -> None:
        filled = getattr(status, "compartments_filled", 0)
        # self.compartments_label.setText(f"Compartments: {filled}/8 filled")
