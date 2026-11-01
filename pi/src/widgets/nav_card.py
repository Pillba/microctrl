from __future__ import annotations

from pathlib import Path

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtGui import QColor, QIcon, QPainter, QPen, QPixmap
from PyQt5.QtWidgets import QHBoxLayout, QLabel, QPushButton, QSizePolicy, QVBoxLayout


ICON_PATHS = {
    "View Schedule": "calendar-clock.svg",
    "Manual Dispense": "pill.svg",
    "View Log": "clipboard-list.svg",
    "Medication Inventory": "pill.svg",
    "Add Medication": "pill.svg",
    "Remove Medication": "pill.svg",
    "Refill Medication": "pill.svg",
    "Device Settings": "settings.svg",
}


class NavCardButton(QPushButton):
    clicked_index = pyqtSignal(int)

    def __init__(self, title: str, subtitle: str, color: str, index: int, parent=None) -> None:
        super().__init__(parent)
        self.index = index
        self._color = QColor(color)
        self.setProperty("class", "nav-card")
        self.setMinimumHeight(72)
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setCursor(Qt.PointingHandCursor)

        layout = QHBoxLayout(self)
        layout.setContentsMargins(14, 10, 12, 10)
        layout.setSpacing(10)

        icon_row = QLabel()
        icon_row.setMinimumSize(48, 48)
        icon_row.setMaximumSize(48, 48)
        icon_row.setAlignment(Qt.AlignCenter)
        icon_path = Path(__file__).with_name("icons") / ICON_PATHS[title]
        icon_pixmap = QIcon(str(icon_path)).pixmap(32, 32)
        white_icon = QPixmap(icon_pixmap.size())
        white_icon.fill(Qt.transparent)
        icon_painter = QPainter(white_icon)
        icon_painter.drawPixmap(0, 0, icon_pixmap)
        icon_painter.setCompositionMode(QPainter.CompositionMode_SourceIn)
        icon_painter.fillRect(white_icon.rect(), Qt.white)
        icon_painter.end()
        icon_row.setPixmap(white_icon)
        icon_row.setStyleSheet(
            "background: rgba(255, 255, 255, 0.18);"
            "border-radius: 12px;"
        )
        self.title_label = QLabel(title)
        self.title_label.setProperty("class", "nav-title")
        self.title_label.setStyleSheet("font-size: 20px; font-weight: 700; color: #FFFFFF;")

        self.subtitle_label = QLabel(subtitle)
        self.subtitle_label.setProperty("class", "nav-subtitle")

        text_column = QVBoxLayout()
        text_column.setSpacing(1)
        text_column.addWidget(self.title_label)
        text_column.addWidget(self.subtitle_label)

        self.chevron = QLabel(">")
        self.chevron.setProperty("class", "nav-chevron")
        self.chevron.setAlignment(Qt.AlignRight | Qt.AlignVCenter)

        layout.addWidget(icon_row)
        layout.addLayout(text_column, 1)
        layout.addWidget(self.chevron)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setPen(Qt.NoPen)
        painter.setBrush(self._color)
        painter.drawRoundedRect(self.rect(), 12, 12)
        if self.hasFocus():
            painter.setBrush(Qt.NoBrush)
            painter.setPen(QPen(QColor("#030ffc"), 3))
            painter.drawRoundedRect(self.rect().adjusted(2, 2, -2, -2), 10, 10)
        super().paintEvent(event)
