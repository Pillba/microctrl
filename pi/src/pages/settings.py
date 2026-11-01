from __future__ import annotations

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QScrollArea,
    QSlider,
    QVBoxLayout,
    QWidget,
)

from db import fetch_setting, set_setting
from widgets.card import CardWidget
from widgets.toggle_switch import ToggleSwitchWidget
from widgets.top_bar import TopBar


class SettingsWidget(QWidget):
    go_back = pyqtSignal()
    settings_changed = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        top_bar = TopBar("Settings")
        top_bar.back_clicked.connect(self.go_back.emit)
        layout.addWidget(top_bar)

        body = QWidget()
        body_layout = QVBoxLayout(body)
        body_layout.setContentsMargins(12, 12, 12, 12)
        body_layout.setSpacing(10)

        connectivity = CardWidget()
        conn_layout = QVBoxLayout(connectivity)
        conn_layout.setContentsMargins(12, 12, 12, 12)
        conn_layout.setSpacing(8)
        conn_layout.addWidget(self._section_title("Connectivity"))
        wifi_row = QHBoxLayout()
        wifi_label = QLabel("Wi-Fi")
        wifi_row.addWidget(wifi_label, 1)
        self.wifi_toggle = ToggleSwitchWidget()
        self.wifi_toggle.setChecked(self._setting_bool("wifi", True))
        self.wifi_toggle.toggled.connect(
            lambda checked: self._save_setting("wifi", checked)
        )
        wifi_row.addWidget(self.wifi_toggle)
        conn_layout.addLayout(wifi_row)
        self.wifi_ssid = QLineEdit(fetch_setting("wifi_ssid", "Home network"))
        self.wifi_ssid.setPlaceholderText("Network name")
        self.wifi_ssid.setMaxLength(64)
        self.wifi_ssid.editingFinished.connect(self._save_wifi_ssid)
        conn_layout.addWidget(self.wifi_ssid)
        body_layout.addWidget(connectivity)

        notifications = CardWidget()
        notif_layout = QVBoxLayout(notifications)
        notif_layout.setContentsMargins(12, 12, 12, 12)
        notif_layout.setSpacing(10)
        notif_layout.addWidget(self._section_title("Notifications"))
        row = QHBoxLayout()
        left = QVBoxLayout()
        left.addWidget(QLabel("Refill Alert"))
        left.addWidget(QLabel("Notify when pills are low"))
        row.addLayout(left, 1)
        self.refill_alert = ToggleSwitchWidget()
        self.refill_alert.setChecked(self._setting_bool("refill_alert", True))
        self.refill_alert.toggled.connect(
            lambda checked: self._save_setting("refill_alert", checked)
        )
        row.addWidget(self.refill_alert)
        notif_layout.addLayout(row)
        volume_row = QHBoxLayout()
        volume_row.addWidget(QLabel("Volume"), 1)
        self.volume_value = QLabel()
        volume_row.addWidget(self.volume_value, 0, Qt.AlignRight)
        notif_layout.addLayout(volume_row)
        self.volume_slider = QSlider(Qt.Horizontal)
        self.volume_slider.setRange(0, 100)
        self.volume_slider.setValue(self._setting_int("volume", 70, 0, 100))
        self.volume_slider.valueChanged.connect(self._save_volume)
        notif_layout.addWidget(self.volume_slider)
        self._update_volume_label(self.volume_slider.value())
        body_layout.addWidget(notifications)

        feedback = CardWidget()
        feedback_layout = QVBoxLayout(feedback)
        feedback_layout.setContentsMargins(12, 12, 12, 12)
        feedback_layout.setSpacing(8)
        feedback_layout.addWidget(self._section_title("Navigation Feedback"))
        self.focus_blink = self._add_toggle_row(
            feedback_layout,
            "Blink focus indicator",
            "Make the selected control visibly blink",
            "focus_blink",
            True,
        )
        self.navigation_beep = self._add_toggle_row(
            feedback_layout,
            "Navigation beep",
            "Play a short sound when navigating",
            "navigation_beep",
            False,
        )
        body_layout.addWidget(feedback)

        system = CardWidget()
        sys_layout = QVBoxLayout(system)
        sys_layout.setContentsMargins(12, 12, 12, 12)
        sys_layout.setSpacing(8)
        sys_layout.addWidget(self._section_title("Device"))
        info = QLabel("Firmware v0.1.0 · Serial #MD-00001")
        info.setStyleSheet("font-size: 11px; color: #64748B;")
        sys_layout.addWidget(info)
        body_layout.addWidget(system)

        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(body)
        layout.addWidget(scroll)

    @staticmethod
    def _section_title(text: str) -> QLabel:
        label = QLabel(text)
        label.setProperty("class", "card-title")
        return label

    def _add_toggle_row(
        self,
        parent_layout: QVBoxLayout,
        title: str,
        subtitle: str,
        key: str,
        default: bool,
    ) -> ToggleSwitchWidget:
        row = QHBoxLayout()
        left = QVBoxLayout()
        left.addWidget(QLabel(title))
        left.addWidget(QLabel(subtitle))
        row.addLayout(left, 1)
        toggle = ToggleSwitchWidget()
        toggle.setChecked(self._setting_bool(key, default))
        toggle.toggled.connect(lambda checked, setting_key=key: self._save_setting(setting_key, checked))
        row.addWidget(toggle)
        parent_layout.addLayout(row)
        return toggle

    @staticmethod
    def _setting_bool(key: str, default: bool) -> bool:
        return fetch_setting(key, "1" if default else "0") == "1"

    @staticmethod
    def _setting_int(key: str, default: int, minimum: int, maximum: int) -> int:
        try:
            return max(minimum, min(maximum, int(fetch_setting(key, str(default)))))
        except ValueError:
            return default

    def _save_setting(self, key: str, value: bool) -> None:
        set_setting(key, "1" if value else "0")
        self.settings_changed.emit()

    def _save_wifi_ssid(self) -> None:
        ssid = self.wifi_ssid.text().strip()
        if ssid:
            set_setting("wifi_ssid", ssid)
            self.wifi_ssid.setText(ssid)
            self.settings_changed.emit()
        else:
            self.wifi_ssid.setText(fetch_setting("wifi_ssid", "Home network"))

    def _save_volume(self, value: int) -> None:
        set_setting("volume", str(value))
        self._update_volume_label(value)
        self.settings_changed.emit()

    def _update_volume_label(self, value: int) -> None:
        self.volume_value.setText(f"{value}%")
