from __future__ import annotations

from PyQt5.QtWidgets import QLabel


class StatusBadge(QLabel):
    def set_status(self, status: str) -> None:
        styles = {
            "taken": "background: #F0FDFA; color: #0D9488;",
            "missed": "background: #FEF2F2; color: #EF4444;",
            "manual": "background: #FFFBEB; color: #F59E0B;",
        }
        self.setStyleSheet(
            "padding: 2px 8px; border-radius: 999px; font-size: 10px; font-weight: 700; "
            + styles.get(status, "background: #E2E8F0; color: #64748B;")
        )
