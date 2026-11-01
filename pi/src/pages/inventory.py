from __future__ import annotations

from PyQt5.QtCore import Qt, pyqtSignal
from PyQt5.QtWidgets import QHBoxLayout, QLabel, QScrollArea, QSizePolicy, QVBoxLayout, QWidget

from db import inventory_chart_data
from widgets.card import CardWidget
from widgets.chart_widgets import MedicationVolumeChart
from widgets.top_bar import TopBar


class InventoryWidget(QWidget):
    go_back = pyqtSignal()
    go_edit_inventory = pyqtSignal()

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        top_bar = TopBar("Medication Inventory")
        top_bar.back_clicked.connect(self.go_back.emit)
        layout.addWidget(top_bar)

        body = QWidget()
        body.setMinimumWidth(0)
        body.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        body_layout = QVBoxLayout(body)
        body_layout.setContentsMargins(12, 12, 12, 12)
        body_layout.setSpacing(10)

        chart_card = CardWidget()
        chart_layout = QVBoxLayout(chart_card)
        chart_layout.setContentsMargins(12, 12, 12, 12)
        chart_layout.setSpacing(8)

        chart_title = QLabel("REMAINING VOLUME")
        chart_title.setProperty("class", "micro-label")
        chart_layout.addWidget(chart_title)

        explanation = QLabel("Each bar shows the percentage remaining in its canister.")
        explanation.setProperty("class", "caption")
        explanation.setWordWrap(True)
        chart_layout.addWidget(explanation)

        self.volume_chart = MedicationVolumeChart(
            inventory_chart_data(),
            chart_card,
        )
        self.volume_chart.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)
        chart_layout.addWidget(self.volume_chart)
        body_layout.addWidget(chart_card)

        legend_card = CardWidget()
        legend_layout = QHBoxLayout(legend_card)
        legend_layout.setContentsMargins(12, 10, 12, 10)
        legend_layout.setSpacing(12)
        legend_label = QLabel("LOW STOCK")
        legend_label.setProperty("class", "micro-label")
        legend_layout.addWidget(legend_label)
        legend_text = QLabel("Red ≤ 10%   Amber ≤ 25%   Teal > 25%")
        legend_text.setProperty("class", "caption")
        legend_text.setWordWrap(True)
        legend_layout.addWidget(legend_text, 1)
        body_layout.addWidget(legend_card)

        edit_button = QLabel("Edit Inventory")
        edit_button.setAlignment(Qt.AlignCenter)
        edit_button.setStyleSheet(
            "background: #0D9488; color: #FFFFFF; border-radius: 12px; "
            "font-size: 12px; font-weight: 700; min-height: 40px;"
        )
        edit_button.mousePressEvent = lambda event: self.go_edit_inventory.emit()
        body_layout.addWidget(edit_button)
        body_layout.addStretch(1)

        scroll_area = QScrollArea()
        scroll_area.setFrameShape(QScrollArea.NoFrame)
        scroll_area.setHorizontalScrollBarPolicy(Qt.ScrollBarAlwaysOff)
        scroll_area.setVerticalScrollBarPolicy(Qt.ScrollBarAsNeeded)
        scroll_area.setWidgetResizable(True)
        scroll_area.setWidget(body)
        layout.addWidget(scroll_area, 1)

    def showEvent(self, event) -> None:
        super().showEvent(event)
        self.volume_chart.load_from_db()
