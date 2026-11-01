from __future__ import annotations

from PyQt5.QtCore import (
    QEasingCurve,
    QPropertyAnimation,
    QRectF,
    QSize,
    Qt,
    pyqtProperty,
)
from PyQt5.QtGui import QColor, QPainter, QPen
from PyQt5.QtWidgets import QAbstractButton


class ToggleSwitchWidget(QAbstractButton):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setCheckable(True)
        self.setCursor(Qt.PointingHandCursor)
        self._thumb_x = 2.0
        self._animation = QPropertyAnimation(self, b"thumb_x", self)
        self._animation.setDuration(150)
        self._animation.setEasingCurve(QEasingCurve.InOutQuad)
        self.toggled.connect(self._on_toggled)

    def sizeHint(self) -> QSize:
        return QSize(44, 26)

    def minimumSizeHint(self) -> QSize:
        return self.sizeHint()

    def paintEvent(self, event) -> None:
        del event
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        track = QRectF(0, 3, 44, 20)
        track_color = QColor("#0D9488" if self.isChecked() else "#CBD5E1")
        painter.setPen(Qt.NoPen)
        painter.setBrush(track_color)
        painter.drawRoundedRect(track, 10, 10)

        painter.setBrush(QColor("#FFFFFF"))
        painter.drawEllipse(QRectF(self._thumb_x, 5, 16, 16))

        if self.hasFocus():
            painter.setBrush(Qt.NoBrush)
            painter.setPen(QPen(QColor("#030FFC"), 2))
            painter.drawRoundedRect(QRectF(0, 1, 44, 24), 5, 5)

    def _on_toggled(self, checked: bool) -> None:
        self._animation.stop()
        self._animation.setStartValue(self._thumb_x)
        self._animation.setEndValue(22.0 if checked else 2.0)
        self._animation.start()

    def get_thumb_x(self) -> float:
        return self._thumb_x

    def set_thumb_x(self, value: float) -> None:
        self._thumb_x = value
        self.update()

    thumb_x = pyqtProperty(float, get_thumb_x, set_thumb_x)
