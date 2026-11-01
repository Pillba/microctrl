from __future__ import annotations

import time
from typing import Any

from PyQt5.QtCore import QObject, QTimer, pyqtSignal


BUTTON_PINS = {
    "up": 17,
    "down": 23,
    "left": 22,
    "right": 27,
    "select": 24,
}



class ButtonController(QObject):
    """Translate physical GPIO buttons and desktop keys into button signals."""

    up_pressed = pyqtSignal()
    down_pressed = pyqtSignal()
    left_pressed = pyqtSignal()
    left_held = pyqtSignal()
    right_pressed = pyqtSignal()
    select_pressed = pyqtSignal()

    def __init__(self, parent: QObject | None = None) -> None:
        super().__init__(parent)
        self._gpio: Any | None = None
        self._last_states: dict[str, bool] = {}
        self._last_pressed_at: dict[str, float] = {}
        self._left_pressed_at: float | None = None
        self._left_hold_emitted = False
        self._timer = QTimer(self)
        self._timer.setInterval(25)
        self._timer.timeout.connect(self._poll_gpio)

        try:
            import RPi.GPIO as gpio
        except ImportError:
            return

        self._gpio = gpio
        gpio.setwarnings(False)
        gpio.setmode(gpio.BCM)
        for name, pin in BUTTON_PINS.items():
            gpio.setup(pin, gpio.IN, pull_up_down=gpio.PUD_UP)
            self._last_states[name] = bool(gpio.input(pin))
        self._timer.start()

    def _poll_gpio(self) -> None:
        if self._gpio is None:
            return

        now = time.monotonic()
        for name, pin in BUTTON_PINS.items():
            is_high = bool(self._gpio.input(pin))
            was_high = self._last_states[name]
            self._last_states[name] = is_high
            if was_high and not is_high and now - self._last_pressed_at.get(name, 0) >= 0.2:
                self._last_pressed_at[name] = now
                if name == "left":
                    self._left_pressed_at = now
                    self._left_hold_emitted = False
                    self.left_pressed.emit()
                else:
                    getattr(self, f"{name}_pressed").emit()
            if name == "left":
                if is_high:
                    self._left_pressed_at = None
                    self._left_hold_emitted = False
                elif (
                    self._left_pressed_at is not None
                    and not self._left_hold_emitted
                    and now - self._left_pressed_at >= 3.0
                ):
                    self._left_hold_emitted = True
                    self.left_held.emit()

    def cleanup(self) -> None:
        self._timer.stop()
        if self._gpio is not None:
            self._gpio.cleanup(list(BUTTON_PINS.values()))
