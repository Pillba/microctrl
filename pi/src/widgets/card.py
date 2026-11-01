from __future__ import annotations

from PyQt5.QtWidgets import QFrame


class CardWidget(QFrame):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setObjectName("card")
        self.setProperty("class", "card")


class SectionCard(QFrame):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setObjectName("section-card")
        self.setProperty("class", "section-card")


class DeviceCard(QFrame):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setObjectName("device-card")
        self.setProperty("class", "device-card")


class StatusBanner(QFrame):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setObjectName("status-banner")
        self.setProperty("class", "status-banner")


class WarningBanner(QFrame):
    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self.setObjectName("warning-banner")
        self.setProperty("class", "warning-banner")
