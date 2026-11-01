from __future__ import annotations

from datetime import datetime, timedelta

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QPushButton,
    QScrollArea,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from db import list_scheduled_medications, weekly_adherence_details
from widgets.card import CardWidget
from widgets.chart_widgets import MedicationScheduleChart
from widgets.top_bar import TopBar


class ScheduleWidget(QWidget):
    go_back = pyqtSignal()
    go_edit = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        top_bar = TopBar("Schedule")
        top_bar.back_clicked.connect(self.go_back.emit)
        layout.addWidget(top_bar)

        body = QWidget()
        body_layout = QVBoxLayout(body)
        body_layout.setContentsMargins(12, 12, 12, 12)
        body_layout.setSpacing(10)

        self._today = datetime.now().date()
        self._selected_date = self._today

        adherence_card = CardWidget()
        adherence_card.setMinimumWidth(0)
        adherence_card.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        adherence_layout = QVBoxLayout(adherence_card)
        adherence_layout.setContentsMargins(12, 12, 12, 12)
        adherence_layout.setSpacing(8)
        adherence_label = QLabel("WEEKLY ADHERENCE")
        adherence_label.setProperty("class", "micro-label")
        adherence_layout.addWidget(adherence_label)
        self.adherence_chart = MedicationScheduleChart(
            weekly_adherence_details(reference_date=self._selected_date),
            self,
        )
        self.adherence_chart.day_clicked.connect(self._select_chart_day)
        adherence_layout.addWidget(self.adherence_chart)

        body_layout.addWidget(adherence_card)

        self._date_back = QPushButton("‹")
        self._date_forward = QPushButton("›")
        self._date_back.setStyleSheet("font-size: 20px; font-weight: bold;")
        self._date_forward.setStyleSheet("font-size: 20px; font-weight: bold;")
        self._date_label = QLabel()
        self._date_label.setAlignment(Qt.AlignCenter)
        self._date_back.setFixedWidth(32)
        self._date_forward.setFixedWidth(32)
        self._date_back.clicked.connect(lambda: self._change_date(-1))
        self._date_forward.clicked.connect(lambda: self._change_date(1))
        date_navigation = QHBoxLayout()
        date_navigation.setContentsMargins(0, 0, 0, 0)
        date_navigation.addWidget(self._date_back)
        date_navigation.addWidget(self._date_label, 1)
        date_navigation.addWidget(self._date_forward)
        body_layout.addLayout(date_navigation)

        self._medications_layout = QVBoxLayout()
        self._medications_layout.setSpacing(10)
        body_layout.addLayout(self._medications_layout)
        self._populate_medications()

        add_button = QLabel("Edit Schedule")
        add_button.setAlignment(Qt.AlignCenter)
        add_button.setStyleSheet("background: #0D9488; color: #FFFFFF; border-radius: 12px; font-size: 12px; font-weight: 700; min-height: 40px;")
        add_button.mousePressEvent = lambda event: self.go_edit.emit()
        body_layout.addWidget(add_button)
        body_layout.addStretch(1)

        body.setMinimumWidth(0)
        body.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        scroll_area = QScrollArea()
        scroll_area.setFrameShape(QScrollArea.NoFrame)
        scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll_area.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        scroll_area.setWidgetResizable(True)
        scroll_area.setWidget(body)
        layout.addWidget(scroll_area, 1)

    def _populate_medications(self) -> None:
        while self._medications_layout.count():
            item = self._medications_layout.takeAt(0)
            if item.widget() is not None:
                item.widget().deleteLater()

        for medication in list_scheduled_medications(self._selected_date):
            row_card = CardWidget()
            row_card.setMinimumWidth(0)
            row_card.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
            row_layout = QHBoxLayout(row_card)
            row_layout.setContentsMargins(12, 10, 12, 10)
            row_layout.setSpacing(10)

            text_column = QVBoxLayout()
            text_column.setSpacing(2)
            name_label = QLabel(medication.name)
            name_label.setProperty("class", "card-title")
            frequency = (
                f"every {medication.frequency_hours}h"
                if medication.frequency_hours
                else "as needed"
            )
            dose_label = QLabel(
                f"{medication.strength or 'Unspecified'} · "
                f"Take {medication.dose_qty} · {frequency} · "
                f"{medication.remaining_quantity}/{medication.total_quantity} left"
            )
            dose_label.setProperty("class", "caption")
            text_column.addWidget(name_label)
            text_column.addWidget(dose_label)

            if medication.taken_today:
                checkmark = QLabel("✓")
                checkmark.setAlignment(Qt.AlignCenter)
                checkmark.setFixedSize(24, 24)
                checkmark.setStyleSheet(
                    "background: #CCFBF1; color: #0D9488; border-radius: 12px; "
                    "font-size: 14px; font-weight: 700;"
                )
                row_layout.addWidget(checkmark)

            row_layout.addLayout(text_column, 1)
            self._medications_layout.addWidget(row_card)

        self._date_label.setText(
            "Today"
            if self._selected_date == self._today
            else self._selected_date.strftime("%a, %b %-d")
        )
        self._date_label.setStyleSheet("font-size: 20px; font-weight: bold;")
        self._date_back.setEnabled(self._selected_date > self._today - timedelta(days=7))
        self._date_forward.setEnabled(self._selected_date < self._today + timedelta(days=7))
        self.adherence_chart.set_selected_day(self._selected_date.weekday())

    def _select_chart_day(self, day_index: int) -> None:
        monday = self._selected_date - timedelta(days=self._selected_date.weekday())
        selected_date = monday + timedelta(days=day_index)
        if self._today - timedelta(days=7) <= selected_date <= self._today + timedelta(days=7):
            self._selected_date = selected_date
            self._populate_medications()

    def _change_date(self, day_delta: int) -> None:
        new_date = self._selected_date + timedelta(days=day_delta)
        if self._today - timedelta(days=7) <= new_date <= self._today + timedelta(days=7):
            self._selected_date = new_date
            self.adherence_chart.set_adherence_data(
                weekly_adherence_details(reference_date=self._selected_date)
            )
            self._populate_medications()

    def showEvent(self, event) -> None:
        super().showEvent(event)
        self.adherence_chart.set_adherence_data(
            weekly_adherence_details(reference_date=self._selected_date)
        )
        self._populate_medications()
