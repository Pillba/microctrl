from __future__ import annotations

import os
import sys
from enum import IntEnum

from PyQt5.QtCore import QObject, Qt, QEvent, QTimer
from PyQt5.QtGui import QFontDatabase
from PyQt5.QtWidgets import (
    QApplication,
    QAbstractButton,
    QMainWindow,
    QPushButton,
    QStackedWidget,
)

from buttons import ButtonController
from pages.dispense import DispenseWidget
from pages.home import HomeWidget
from pages.inventory import InventoryWidget
from pages.inventory_actions import InventoryActionsWidget
from pages.inventory_form import AddInventoryWidget, EditScheduleWidget
from pages.remove_inventory import RemoveInventoryWidget
from pages.refill_inventory import RefillInventoryWidget
from pages.schedule import ScheduleWidget
from pages.settings import SettingsWidget
from serial_worker import SerialWorker
from db import fetch_setting, initialize


class Page(IntEnum):
    HOME = 0
    SCHEDULE = 1
    DISPENSE = 2
    INVENTORY = 3
    SETTINGS = 4
    INVENTORY_ACTIONS = 5
    ADD_INVENTORY = 6
    REMOVE_INVENTORY = 7
    REFILL_INVENTORY = 8
    EDIT_SCHEDULE = 9


class MainWindow(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setObjectName("root")
        self.setFixedSize(480, 800)
        self.setWindowFlags(Qt.FramelessWindowHint)

        self._load_fonts()
        self._load_styles()

        self.stack = QStackedWidget()
        self.home = HomeWidget()
        self.schedule = ScheduleWidget()
        self.dispense = DispenseWidget()
        self.inventory = InventoryWidget()
        self.inventory_actions = InventoryActionsWidget()
        self.add_inventory = AddInventoryWidget()
        self.remove_inventory = RemoveInventoryWidget()
        self.refill_inventory = RefillInventoryWidget()
        self.edit_schedule = EditScheduleWidget()
        self.settings_page = SettingsWidget()

        for widget in [
            self.home,
            self.schedule,
            self.dispense,
            self.inventory,
            self.settings_page,
            self.inventory_actions,
            self.add_inventory,
            self.remove_inventory,
            self.refill_inventory,
            self.edit_schedule,
        ]:
            self.stack.addWidget(widget)

        self.setCentralWidget(self.stack)

        self.home.navigate.connect(self.go_to)
        self.schedule.go_back.connect(lambda: self.go_to(Page.HOME))
        self.schedule.go_edit.connect(lambda: self.go_to(Page.EDIT_SCHEDULE))
        self.dispense.go_back.connect(lambda: self.go_to(Page.HOME))
        self.inventory.go_back.connect(lambda: self.go_to(Page.HOME))
        self.inventory.go_edit_inventory.connect(
            lambda: self.go_to(Page.INVENTORY_ACTIONS)
        )
        self.inventory_actions.go_back.connect(lambda: self.go_to(Page.INVENTORY))
        self.inventory_actions.add_requested.connect(
            lambda: self.go_to(Page.ADD_INVENTORY)
        )
        self.inventory_actions.remove_requested.connect(
            lambda: self.go_to(Page.REMOVE_INVENTORY)
        )
        self.inventory_actions.refill_requested.connect(
            lambda: self.go_to(Page.REFILL_INVENTORY)
        )
        self.add_inventory.go_back.connect(lambda: self.go_to(Page.INVENTORY_ACTIONS))
        self.remove_inventory.go_back.connect(
            lambda: self.go_to(Page.INVENTORY_ACTIONS)
        )
        self.refill_inventory.go_back.connect(
            lambda: self.go_to(Page.INVENTORY_ACTIONS)
        )
        self.edit_schedule.go_back.connect(lambda: self.go_to(Page.SCHEDULE))
        self.settings_page.go_back.connect(lambda: self.go_to(Page.HOME))

        self.worker = SerialWorker()
        self.worker.device_status.connect(self.home.on_device_status)
        self.dispense.set_worker(self.worker)
        self.worker.start()
        self.buttons = ButtonController(self)
        self.buttons.up_pressed.connect(self._navigate_previous)
        self.buttons.down_pressed.connect(self._navigate_next)
        self.buttons.left_held.connect(self._go_back)
        self.buttons.select_pressed.connect(self._activate_focused)
        self._left_hold_timer = QTimer(self)
        self._left_hold_timer.setSingleShot(True)
        self._left_hold_timer.setInterval(3000)
        self._left_hold_timer.timeout.connect(self._go_back)
        QApplication.instance().installEventFilter(self)
        QApplication.instance().focusChanged.connect(self._on_focus_changed)
        self._focus_blink_timer = QTimer(self)
        self._focus_blink_timer.setInterval(500)
        self._focus_blink_timer.timeout.connect(self._toggle_focus_indicator)
        self._focus_blink_timer.start()
        self._focus_indicator_visible = True
        self.stack.currentChanged.connect(lambda _: self._focus_first_control())
        self._focus_first_control()

    def eventFilter(self, watched: QObject, event: QEvent) -> bool:
        if event.type() == QEvent.KeyPress:
            if event.key() == Qt.Key_Left:
                if not event.isAutoRepeat():
                    self._left_hold_timer.start()
                return True
            key_actions = {
                Qt.Key_Up: self._navigate_previous,
                Qt.Key_Down: self._navigate_next,
                Qt.Key_Return: self._activate_focused,
                Qt.Key_Enter: self._activate_focused,
            }
            action = key_actions.get(event.key())
            if action is not None:
                action()
                return True
        if event.type() == QEvent.KeyRelease and event.key() == Qt.Key_Left:
            self._left_hold_timer.stop()
            return True
        return super().eventFilter(watched, event)

    def _focus_first_control(self) -> None:
        focusable = self.stack.currentWidget().findChildren(QPushButton)
        if focusable:
            focusable[0].setFocus()

    def _focus_previous(self) -> None:
        self.focusPreviousChild()

    def _focus_next(self) -> None:
        self.focusNextChild()

    def _navigate_previous(self) -> None:
        self._focus_previous()
        self._play_navigation_beep()

    def _navigate_next(self) -> None:
        self._focus_next()
        self._play_navigation_beep()

    def _activate_focused(self) -> None:
        focused = QApplication.focusWidget()
        if isinstance(focused, QAbstractButton):
            focused.click()
            self._play_navigation_beep()

    def _play_navigation_beep(self) -> None:
        if fetch_setting("navigation_beep", "0") == "1":
            QApplication.beep()

    def _on_focus_changed(self, old, current) -> None:
        if old is not None:
            old.setProperty("focus-visible", False)
            old.style().unpolish(old)
            old.style().polish(old)
        if current is not None:
            current.setProperty("focus-visible", self._focus_indicator_visible)
            current.style().unpolish(current)
            current.style().polish(current)

    def _toggle_focus_indicator(self) -> None:
        if fetch_setting("focus_blink", "1") != "1":
            self._focus_indicator_visible = True
        else:
            self._focus_indicator_visible = not self._focus_indicator_visible
        current = QApplication.focusWidget()
        if current is not None:
            self._on_focus_changed(None, current)

    def _go_back(self) -> None:
        parent_pages = {
            Page.SCHEDULE: Page.HOME,
            Page.DISPENSE: Page.HOME,
            Page.INVENTORY: Page.HOME,
            Page.SETTINGS: Page.HOME,
            Page.INVENTORY_ACTIONS: Page.INVENTORY,
            Page.ADD_INVENTORY: Page.INVENTORY_ACTIONS,
            Page.REMOVE_INVENTORY: Page.INVENTORY_ACTIONS,
            Page.REFILL_INVENTORY: Page.INVENTORY_ACTIONS,
            Page.EDIT_SCHEDULE: Page.SCHEDULE,
        }
        current_page = Page(self.stack.currentIndex())
        destination = parent_pages.get(current_page)
        if destination is not None:
            self.go_to(destination)

    def _load_fonts(self) -> None:
        for font_name in [
            "Nunito-Regular.ttf",
            "Nunito-SemiBold.ttf",
            "Nunito-Bold.ttf",
        ]:
            font_path = os.path.join(os.path.dirname(__file__), "fonts", font_name)
            if os.path.exists(font_path):
                QFontDatabase.addApplicationFont(font_path)

    def _load_styles(self) -> None:
        style_path = os.path.join(os.path.dirname(__file__), "styles.qss")
        with open(style_path, encoding="utf-8") as handle:
            self.setStyleSheet(handle.read())

    def go_to(self, page: int | Page) -> None:
        self.stack.setCurrentIndex(int(page))

    def closeEvent(self, event) -> None:
        self.buttons.cleanup()
        self.worker.stop()
        super().closeEvent(event)


def main() -> int:
    initialize()

    app = QApplication(sys.argv)
    window = MainWindow()
    screens = app.screens()
    screen_index = int(os.environ.get("PILLBA_SCREEN_INDEX", "1"))
    if 0 <= screen_index < len(screens):
        target_screen = screens[screen_index]
    else:
        target_screen = app.primaryScreen()
        print(
            f"Screen index {screen_index} is unavailable; "
            f"using primary screen instead.",
            file=sys.stderr,
        )
    window.setGeometry(target_screen.geometry())
    window.showFullScreen()
    return app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
