from __future__ import annotations

from datetime import datetime
from pathlib import Path
import sys

# Ensure project root is importable when running `python3 widgets/chart_widgets.py` directly
_PROJECT_ROOT = Path(__file__).resolve().parent.parent
if str(_PROJECT_ROOT) not in sys.path:
    sys.path.insert(0, str(_PROJECT_ROOT))

from PyQt5.QtCore import QPointF, QRectF, QSize, Qt, pyqtSignal
from PyQt5.QtGui import QBrush, QColor, QFont, QFontMetricsF, QPainter, QPainterPath, QPen
from PyQt5.QtWidgets import (
    QApplication,
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from db import (
    DB_PATH,
    connect,
    initialize,
    inventory_chart_data,
    weekly_adherence_details,
)


class MedicationScheduleChart(QWidget):
    """Apple Health-inspired weekly medication adherence ring chart (Mon-Sun)."""

    day_clicked = pyqtSignal(int)

    # Theme palette aligned with Pillba QSS
    COLOR_ACCENT = QColor("#0D9488")         # Teal primary (>0% adherence / 100% solid)
    COLOR_ACCENT_LIGHT = QColor("#14B8A6")   # Brighter teal for partial ring arc
    COLOR_PARTIAL_BG = QColor(13, 148, 136, 32)
    COLOR_TRACK = QColor("#E2E8F0")          # Incomplete / unscheduled ring track
    COLOR_NEUTRAL_FILL = QColor("#F1F5F9")   # 0 scheduled subtle center fill
    COLOR_TODAY_BG = QColor(13, 148, 136, 22)
    COLOR_TODAY_BORDER = QColor(13, 148, 136, 90)
    COLOR_TEXT_PRIMARY = QColor("#0F172A")
    COLOR_TEXT_MUTED = QColor("#64748B")
    COLOR_WHITE = QColor("#FFFFFF")

    DEFAULT_DAYS = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]

    def __init__(self, data: list[dict] | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._data: list[dict] = []
        self._selected_index = -1
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        self.setMinimumSize(220, 88)
        if data is not None:
            self.set_adherence_data(data)
        elif DB_PATH.exists():
            self.load_from_db(DB_PATH)
        else:
            self.set_adherence_data(self._default_empty_week())

    def sizeHint(self) -> QSize:
        return QSize(296, 100)

    @classmethod
    def _default_empty_week(cls) -> list[dict]:
        today_idx = datetime.now().weekday()
        return [
            {
                "day": day,
                "scheduled": 0,
                "taken": 0,
                "is_today": idx == today_idx,
            }
            for idx, day in enumerate(cls.DEFAULT_DAYS)
        ]

    def load_from_db(self, db_path: Path = DB_PATH) -> None:
        """Load weekly adherence data from the SQLite `log` table."""
        self.set_adherence_data(weekly_adherence_details(db_path))

    def set_adherence_data(self, data: list[dict]) -> None:
        """Update weekly adherence state and trigger a repaint.

        Expected item format:
            {"day": "Mon", "scheduled": 3, "taken": 2, "is_today": False}
        """
        normalized: list[dict] = [
            {
                "day": day,
                "initial": day[0],
                "scheduled": 0,
                "taken": 0,
                "is_today": False,
            }
            for day in self.DEFAULT_DAYS
        ]
        source = data if data else self._default_empty_week()
        for idx, item in enumerate(source[:7]):
            if not isinstance(item, dict):
                continue
            day_str = str(item.get("day") or self.DEFAULT_DAYS[idx]).strip()
            day_index = next(
                (
                    day_idx
                    for day_idx, day_name in enumerate(self.DEFAULT_DAYS)
                    if day_name.lower() == day_str.lower()
                ),
                idx,
            )
            scheduled = max(0, int(item.get("scheduled", 0)))
            taken = max(0, min(scheduled, int(item.get("taken", 0)))) if scheduled > 0 else 0
            is_today = bool(item.get("is_today", False))

            normalized[day_index] = {
                "day": self.DEFAULT_DAYS[day_index],
                "initial": self.DEFAULT_DAYS[day_index][0],
                "scheduled": scheduled,
                "taken": taken,
                "is_today": is_today,
            }

        self._data = normalized
        self.update()

    def adherence_data(self) -> list[dict]:
        return list(self._data)

    def set_selected_day(self, day_index: int | None) -> None:
        """Highlight a weekday and keep it synchronized with the card list."""
        self._selected_index = day_index if day_index is not None and 0 <= day_index < 7 else -1
        self.update()

    def mousePressEvent(self, event) -> None:
        if event.button() != Qt.LeftButton:
            super().mousePressEvent(event)
            return

        pad_x = max(4.0, self.width() * 0.02)
        slot_w = max(1.0, (self.width() - 2.0 * pad_x) / 7.0)
        day_index = int((event.x() - pad_x) // slot_w)
        if 0 <= day_index < 7:
            self.set_selected_day(day_index)
            self.day_clicked.emit(day_index)
        event.accept()

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)
        painter.setRenderHint(QPainter.TextAntialiasing, True)

        bounds = QRectF(self.rect())
        if bounds.width() <= 20 or bounds.height() <= 20:
            return

        num_days = 7
        pad_x = max(4.0, bounds.width() * 0.02)
        pad_y = max(4.0, bounds.height() * 0.06)
        usable_w = max(1.0, bounds.width() - 2.0 * pad_x)
        usable_h = max(1.0, bounds.height() - 2.0 * pad_y)
        slot_w = usable_w / num_days

        # Reserve vertical regions: top initial label, middle ring, bottom dose ratio
        top_label_h = min(20.0, max(13.0, usable_h * 0.24))
        bottom_label_h = min(18.0, max(11.0, usable_h * 0.20))
        ring_area_h = max(18.0, usable_h - top_label_h - bottom_label_h - 4.0)

        max_diameter = min(slot_w * 0.76, ring_area_h * 0.88, 44.0)
        diameter = max(16.0, max_diameter)
        radius = diameter / 2.0
        ring_stroke = max(3.0, min(5.5, diameter * 0.14))

        initial_font = QFont(self.font())
        initial_font.setPixelSize(int(max(10.0, min(13.0, top_label_h * 0.72))))

        ratio_font = QFont(self.font())
        ratio_font.setPixelSize(int(max(9.0, min(11.0, bottom_label_h * 0.68))))
        ratio_font.setWeight(QFont.DemiBold)

        for idx, item in enumerate(self._data):
            slot_x = pad_x + idx * slot_w
            slot_rect = QRectF(slot_x + 1.5, pad_y, slot_w - 3.0, usable_h)

            is_today = item["is_today"]
            scheduled = item["scheduled"]
            taken = item["taken"]
            ratio = (taken / scheduled) if scheduled > 0 else 0.0

            # Highlight only the selected day; today is indicated by its label styling.
            if idx == self._selected_index:
                painter.setPen(QPen(QColor("#030ffc"), 1.8))
                painter.setBrush(QBrush(QColor(13, 148, 136, 35)))
                painter.drawRoundedRect(slot_rect, 10.0, 10.0)

            # 2. Day Initial (M, T, W, T, F, S, S)
            initial_font.setBold(is_today or idx == self._selected_index)
            painter.setFont(initial_font)
            painter.setPen(
                self.COLOR_ACCENT
                if is_today or idx == self._selected_index
                else self.COLOR_TEXT_MUTED
            )
            label_rect = QRectF(slot_x, pad_y + 1.0, slot_w, top_label_h)
            painter.drawText(label_rect, Qt.AlignCenter, item["initial"])

            # 3. Circular adherence node / ring
            cx = slot_x + slot_w / 2.0
            cy = pad_y + top_label_h + 2.0 + ring_area_h / 2.0
            circle_rect = QRectF(cx - radius, cy - radius, diameter, diameter)
            arc_rect = circle_rect.adjusted(
                ring_stroke / 2.0,
                ring_stroke / 2.0,
                -ring_stroke / 2.0,
                -ring_stroke / 2.0,
            )

            if scheduled == 0:
                # Neutral state for days with 0 scheduled doses
                painter.setPen(Qt.NoPen)
                painter.setBrush(QBrush(self.COLOR_NEUTRAL_FILL))
                painter.drawEllipse(circle_rect)

                neutral_pen = QPen(self.COLOR_TRACK, max(2.0, ring_stroke * 0.75))
                neutral_pen.setCapStyle(Qt.RoundCap)
                painter.setPen(neutral_pen)
                painter.setBrush(Qt.NoBrush)
                painter.drawEllipse(arc_rect)

                # Subtle neutral dash in center
                dash_w = max(4.0, diameter * 0.22)
                dash_pen = QPen(QColor("#94A3B8"), 1.8, Qt.SolidLine, Qt.RoundCap)
                painter.setPen(dash_pen)
                painter.drawLine(QPointF(cx - dash_w / 2.0, cy), QPointF(cx + dash_w / 2.0, cy))

            elif taken >= scheduled:
                # 100% completed: crisp solid accent circle + white checkmark
                painter.setPen(Qt.NoPen)
                painter.setBrush(QBrush(self.COLOR_ACCENT))
                painter.drawEllipse(circle_rect)

                # Crisp checkmark
                check_pen = QPen(self.COLOR_WHITE, max(2.0, diameter * 0.085))
                check_pen.setCapStyle(Qt.RoundCap)
                check_pen.setJoinStyle(Qt.RoundJoin)
                painter.setPen(check_pen)
                painter.setBrush(Qt.NoBrush)

                r = radius * 0.42
                path = QPainterPath()
                path.moveTo(cx - r * 0.85, cy + r * 0.05)
                path.lineTo(cx - r * 0.18, cy + r * 0.72)
                path.lineTo(cx + r * 0.92, cy - r * 0.58)
                painter.drawPath(path)

            else:
                # Incomplete / partial adherence (0 <= taken < scheduled)
                painter.setPen(Qt.NoPen)
                painter.setBrush(QBrush(self.COLOR_PARTIAL_BG if taken > 0 else QColor(226, 232, 240, 70)))
                painter.drawEllipse(circle_rect)

                # Background ring track
                track_pen = QPen(self.COLOR_TRACK, ring_stroke)
                track_pen.setCapStyle(Qt.RoundCap)
                painter.setPen(track_pen)
                painter.setBrush(Qt.NoBrush)
                painter.drawEllipse(arc_rect)

                # Proportional adherence progress arc (starts at 12 o'clock, clockwise)
                if taken > 0:
                    fill_pen = QPen(self.COLOR_ACCENT, ring_stroke)
                    fill_pen.setCapStyle(Qt.RoundCap)
                    painter.setPen(fill_pen)
                    start_angle = 90 * 16
                    span_angle = -int(round(ratio * 360 * 16))
                    painter.drawArc(arc_rect, start_angle, span_angle)

            # 4. Bottom status text (e.g., "2/3" or "-" for unscheduled)
            painter.setFont(ratio_font)
            status_rect = QRectF(slot_x, pad_y + top_label_h + ring_area_h + 2.0, slot_w, bottom_label_h)
            if scheduled > 0:
                status_text = f"{taken}/{scheduled}"
                if taken >= scheduled:
                    painter.setPen(self.COLOR_ACCENT)
                elif is_today:
                    painter.setPen(self.COLOR_TEXT_PRIMARY)
                else:
                    painter.setPen(self.COLOR_TEXT_MUTED)
            else:
                status_text = "–"
                painter.setPen(QColor("#94A3B8"))
            painter.drawText(status_rect, Qt.AlignCenter, status_text)


class MedicationVolumeChart(QWidget):
    """Dynamic remaining inventory bar chart for 1 to 8 medication canisters."""

    MAX_ITEMS = 8

    # Threshold colors
    COLOR_NORMAL = QColor("#0D9488")      # > 25% remaining (Teal)
    COLOR_WARNING = QColor("#F59E0B")     # <= 25% remaining (Amber)
    COLOR_CRITICAL = QColor("#EF4444")    # <= 10% remaining (Red)

    COLOR_TRACK = QColor("#F1F5F9")
    COLOR_TRACK_BORDER = QColor("#E2E8F0")
    COLOR_TEXT_PRIMARY = QColor("#0F172A")
    COLOR_TEXT_MUTED = QColor("#64748B")
    COLOR_EMPTY_BG = QColor("#F8FAFC")

    def __init__(self, items: list[dict] | None = None, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._items: list[dict] = []
        self.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)
        self.setMinimumSize(220, 110)
        if items is not None:
            self.set_inventory_data(items)
        elif DB_PATH.exists():
            self.load_from_db(DB_PATH)

    def sizeHint(self) -> QSize:
        count = max(1, len(self._items))
        ideal_h = max(120, min(320, count * 36 + 16))
        return QSize(296, ideal_h)

    def load_from_db(
        self,
        db_path: Path = DB_PATH,
        *,
        active_only: bool = False,
        limit: int = MAX_ITEMS,
    ) -> None:
        """Load canister inventory from the SQLite `medications` table."""
        self.set_inventory_data(
            inventory_chart_data(db_path, active_only=active_only, limit=limit)
        )

    def set_inventory_data(self, items: list[dict]) -> None:
        """Set inventory canisters (up to 8) and repaint.

        Expected item format:
            {"name": "Advil", "current": 18, "max": 30, "unit": "pills"}
        """
        normalized: list[dict] = []
        for idx, raw in enumerate((items or [])[: self.MAX_ITEMS]):
            if not isinstance(raw, dict):
                continue
            name = str(raw.get("name") or f"Slot {idx + 1}").strip()
            max_cap = int(raw.get("max", 1))
            if max_cap <= 0:
                raise ValueError(f"Inventory capacity must be positive for {name!r}")
            current = min(max_cap, max(0, int(raw.get("current", 0))))
            unit = str(raw.get("unit") or "pills").strip()
            normalized.append(
                {
                    "name": name,
                    "current": current,
                    "max": max_cap,
                    "unit": unit,
                }
            )
        self._items = normalized
        self.updateGeometry()
        self.update()

    def inventory_data(self) -> list[dict]:
        return list(self._items)

    @classmethod
    def _color_for_ratio(cls, ratio: float) -> QColor:
        if ratio <= 0.10:
            return cls.COLOR_CRITICAL
        if ratio <= 0.25:
            return cls.COLOR_WARNING
        return cls.COLOR_NORMAL

    def paintEvent(self, event) -> None:
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing, True)
        painter.setRenderHint(QPainter.TextAntialiasing, True)

        bounds = QRectF(self.rect())
        if bounds.width() <= 20 or bounds.height() <= 20:
            return

        if not self._items:
            self._draw_empty_state(painter, bounds)
            return

        count = len(self._items)
        pad_x = max(6.0, bounds.width() * 0.025)
        pad_y = max(4.0, bounds.height() * 0.03)
        usable_w = max(1.0, bounds.width() - 2.0 * pad_x)
        usable_h = max(1.0, bounds.height() - 2.0 * pad_y)

        row_pitch = usable_h / count
        # Use stacked two-line layout (label row above full-width bar) when space permits,
        # and automatically switch to compact inline layout when many canisters fit in a tight height.
        stacked_mode = row_pitch >= 28.0

        for idx, item in enumerate(self._items):
            row_top = pad_y + idx * row_pitch
            row_rect = QRectF(pad_x, row_top, usable_w, row_pitch)

            current = item["current"]
            max_cap = item["max"]
            unit = item["unit"]
            ratio = max(0.0, min(1.0, current / max_cap)) if max_cap > 0 else 0.0
            bar_color = self._color_for_ratio(ratio)

            if stacked_mode:
                self._draw_stacked_row(painter, row_rect, item["name"], current, max_cap, unit, ratio, bar_color)
            else:
                self._draw_compact_row(painter, row_rect, item["name"], current, max_cap, unit, ratio, bar_color)

    def _draw_stacked_row(
        self,
        painter: QPainter,
        row_rect: QRectF,
        name: str,
        current: int,
        max_cap: int,
        unit: str,
        ratio: float,
        bar_color: QColor,
    ) -> None:
        row_h = row_rect.height()
        v_pad = max(2.0, min(6.0, row_h * 0.10))
        inner_h = max(14.0, row_h - 2.0 * v_pad)

        text_h = max(12.0, min(18.0, inner_h * 0.52))
        gap_y = max(2.0, min(5.0, inner_h * 0.08))
        bar_h = max(6.0, min(16.0, inner_h - text_h - gap_y))

        content_h = text_h + gap_y + bar_h
        start_y = row_rect.top() + (row_h - content_h) / 2.0

        font_px = int(max(9.0, min(12.0, text_h * 0.78)))
        name_font = QFont(self.font())
        name_font.setPixelSize(font_px)
        name_font.setWeight(QFont.Bold)

        qty_font = QFont(self.font())
        qty_font.setPixelSize(max(9, font_px - 1))
        qty_font.setWeight(QFont.DemiBold)

        qty_text = f"{ratio:.0%}"

        qty_metrics = QFontMetricsF(qty_font)
        qty_w = qty_metrics.horizontalAdvance(qty_text) + 6.0

        text_y = start_y
        name_w = max(20.0, row_rect.width() - qty_w - 6.0)
        name_rect = QRectF(row_rect.left(), text_y, name_w, text_h)
        qty_rect = QRectF(row_rect.right() - qty_w, text_y, qty_w, text_h)

        # Draw medication name (elided if long)
        painter.setFont(name_font)
        painter.setPen(self.COLOR_TEXT_PRIMARY)
        name_metrics = QFontMetricsF(name_font)
        elided_name = name_metrics.elidedText(name, Qt.ElideRight, name_w)
        painter.drawText(name_rect, Qt.AlignLeft | Qt.AlignVCenter, elided_name)

        # Draw quantity label colored by threshold when low/critical
        painter.setFont(qty_font)
        painter.setPen(bar_color if ratio <= 0.25 else self.COLOR_TEXT_MUTED)
        painter.drawText(qty_rect, Qt.AlignRight | Qt.AlignVCenter, qty_text)

        # Draw rounded progress track & fill bar
        bar_y = text_y + text_h + gap_y
        track_rect = QRectF(row_rect.left(), bar_y, row_rect.width(), bar_h)
        self._draw_bar(painter, track_rect, ratio, bar_color)

    def _draw_compact_row(
        self,
        painter: QPainter,
        row_rect: QRectF,
        name: str,
        current: int,
        max_cap: int,
        unit: str,
        ratio: float,
        bar_color: QColor,
    ) -> None:
        row_h = row_rect.height()
        font_px = int(max(8.0, min(11.0, row_h * 0.62)))

        name_font = QFont(self.font())
        name_font.setPixelSize(font_px)
        name_font.setWeight(QFont.Bold)

        qty_font = QFont(self.font())
        qty_font.setPixelSize(max(8, font_px - 1))
        qty_font.setWeight(QFont.DemiBold)

        qty_text = f"{ratio:.0%}"
        qty_metrics = QFontMetricsF(qty_font)
        qty_w = max(36.0, qty_metrics.horizontalAdvance(qty_text) + 4.0)

        label_w = max(54.0, min(110.0, row_rect.width() * 0.34))
        gap_x = 6.0
        bar_w = max(24.0, row_rect.width() - label_w - qty_w - 2.0 * gap_x)

        name_rect = QRectF(row_rect.left(), row_rect.top(), label_w, row_h)
        bar_h = max(5.0, min(10.0, row_h * 0.48))
        bar_y = row_rect.center().y() - bar_h / 2.0
        track_rect = QRectF(name_rect.right() + gap_x, bar_y, bar_w, bar_h)
        qty_rect = QRectF(track_rect.right() + gap_x, row_rect.top(), qty_w, row_h)

        painter.setFont(name_font)
        painter.setPen(self.COLOR_TEXT_PRIMARY)
        elided_name = QFontMetricsF(name_font).elidedText(name, Qt.ElideRight, label_w)
        painter.drawText(name_rect, Qt.AlignLeft | Qt.AlignVCenter, elided_name)

        self._draw_bar(painter, track_rect, ratio, bar_color)

        painter.setFont(qty_font)
        painter.setPen(bar_color if ratio <= 0.25 else self.COLOR_TEXT_MUTED)
        painter.drawText(qty_rect, Qt.AlignRight | Qt.AlignVCenter, qty_text)

    def _draw_bar(self, painter: QPainter, track_rect: QRectF, ratio: float, bar_color: QColor) -> None:
        corner_r = track_rect.height() / 2.0

        # Background track
        painter.setPen(QPen(self.COLOR_TRACK_BORDER, 1.0))
        painter.setBrush(QBrush(self.COLOR_TRACK))
        painter.drawRoundedRect(track_rect, corner_r, corner_r)

        # Filled volume bar
        if ratio > 0.0:
            fill_w = max(track_rect.height(), track_rect.width() * ratio)
            fill_w = min(track_rect.width(), fill_w)
            fill_rect = QRectF(track_rect.left(), track_rect.top(), fill_w, track_rect.height())
            painter.setPen(Qt.NoPen)
            painter.setBrush(QBrush(bar_color))
            painter.drawRoundedRect(fill_rect, corner_r, corner_r)

    def _draw_empty_state(self, painter: QPainter, bounds: QRectF) -> None:
        empty_rect = bounds.adjusted(6.0, 6.0, -6.0, -6.0)
        pen = QPen(self.COLOR_TRACK_BORDER, 1.2, Qt.DashLine)
        painter.setPen(pen)
        painter.setBrush(QBrush(self.COLOR_EMPTY_BG))
        painter.drawRoundedRect(empty_rect, 10.0, 10.0)

        font = QFont(self.font())
        font.setPixelSize(11)
        font.setWeight(QFont.DemiBold)
        painter.setFont(font)
        painter.setPen(self.COLOR_TEXT_MUTED)
        painter.drawText(empty_rect, Qt.AlignCenter | Qt.TextWordWrap, "No medications configured")


# ---------------------------------------------------------------------------
def _ensure_example_db(db_path: Path = DB_PATH) -> None:
    """Initialize a database and seed examples only when it is newly created."""
    is_new_database = not db_path.exists()
    initialize(db_path)
    sql_file = _PROJECT_ROOT / "create_examples.sql"
    if not is_new_database or not sql_file.exists():
        return

    conn = connect(db_path)
    try:
        conn.executescript(sql_file.read_text(encoding="utf-8"))
        conn.commit()
    finally:
        conn.close()


if __name__ == "__main__":
    _ensure_example_db(DB_PATH)

    app = QApplication(sys.argv)

    # Demo container sized for Pillba embedded display (resizable for testing)
    window = QWidget()
    window.setWindowTitle("Pillba – SQLite Chart Widgets Demo")
    window.resize(340, 520)
    window.setStyleSheet(
        """
        QWidget {
            background: #F8FAFC;
            font-family: "Nunito", "Helvetica Neue", Arial, sans-serif;
            color: #0F172A;
        }
        QFrame#card {
            background: #FFFFFF;
            border: 1px solid #E2E8F0;
            border-radius: 12px;
        }
        QLabel#sectionTitle {
            color: #64748B;
            font-size: 10px;
            font-weight: 700;
            letter-spacing: 1px;
            background: transparent;
        }
        QPushButton {
            background: #E2E8F0;
            color: #0F172A;
            border: none;
            border-radius: 8px;
            padding: 6px 8px;
            font-size: 10px;
            font-weight: 700;
        }
        QPushButton:hover {
            background: #CBD5E1;
        }
        """
    )

    root_layout = QVBoxLayout(window)
    root_layout.setContentsMargins(12, 12, 12, 12)
    root_layout.setSpacing(10)

    # 1. MedicationScheduleChart Card (populated from `log` table in pillba.sqlite3)
    schedule_card = QFrame()
    schedule_card.setObjectName("card")
    schedule_layout = QVBoxLayout(schedule_card)
    schedule_layout.setContentsMargins(12, 10, 12, 10)
    schedule_layout.setSpacing(6)

    schedule_title = QLabel("WEEKLY ADHERENCE (SQLITE LOG)")
    schedule_title.setObjectName("sectionTitle")
    schedule_chart = MedicationScheduleChart()
    schedule_chart.setStyleSheet("background: transparent;")
    schedule_chart.load_from_db(DB_PATH)

    schedule_layout.addWidget(schedule_title)
    schedule_layout.addWidget(schedule_chart)
    root_layout.addWidget(schedule_card)

    # 2. MedicationVolumeChart Card (populated from `medications` table in pillba.sqlite3)
    volume_card = QFrame()
    volume_card.setObjectName("card")
    volume_layout = QVBoxLayout(volume_card)
    volume_layout.setContentsMargins(12, 10, 12, 10)
    volume_layout.setSpacing(6)

    volume_title = QLabel("CANISTER INVENTORY (SQLITE MEDICATIONS)")
    volume_title.setObjectName("sectionTitle")
    volume_chart = MedicationVolumeChart()
    volume_chart.setStyleSheet("background: transparent;")

    all_db_meds = inventory_chart_data(DB_PATH, active_only=False, limit=8)
    active_db_meds = inventory_chart_data(DB_PATH, active_only=True, limit=8)
    low_stock_db_meds = [m for m in all_db_meds if m["level_pct"] <= 25.0]
    single_slot_db = all_db_meds[:1]

    db_views = {
        "All DB (8)": all_db_meds,
        "Active (7)": active_db_meds,
        "Low ≤25%": low_stock_db_meds,
        "Slot 1": single_slot_db,
        "Empty": [],
    }
    volume_chart.set_inventory_data(all_db_meds)

    volume_layout.addWidget(volume_title)
    volume_layout.addWidget(volume_chart, 1)
    root_layout.addWidget(volume_card, 1)

    # Interactive filter switcher row querying different subsets of pillba.sqlite3
    btn_row = QHBoxLayout()
    btn_row.setSpacing(5)
    for label, data in db_views.items():
        btn = QPushButton(label)
        btn.clicked.connect(lambda checked=False, d=data: volume_chart.set_inventory_data(d))
        btn_row.addWidget(btn)
    root_layout.addLayout(btn_row)

    window.show()
    sys.exit(app.exec_())