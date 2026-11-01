from __future__ import annotations

import sqlite3
from dataclasses import dataclass
from datetime import date, datetime, timedelta
from pathlib import Path


DB_PATH = Path(__file__).with_name("pillba.sqlite3")
DAYS_OF_WEEK = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]


@dataclass(frozen=True)
class Medication:
    id: int | None
    name: str
    strength: str | None
    slot: int
    dose_qty: int = 1
    frequency_hours: int | None = None
    dose_time: str = "08:00"
    total_quantity: int = 0
    remaining_quantity: int = 0
    active: int = 1
    level_pct: float = 0.0
    taken_today: bool = False


def connect(path: Path = DB_PATH) -> sqlite3.Connection:
    connection = sqlite3.connect(path)
    connection.row_factory = sqlite3.Row
    return connection


def initialize(path: Path = DB_PATH) -> None:
    connection = connect(path)
    try:
        connection.executescript(
            """
            CREATE TABLE IF NOT EXISTS medications (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                strength TEXT,
                slot INTEGER NOT NULL UNIQUE,
                active INTEGER NOT NULL DEFAULT 1,
                dose_qty INTEGER NOT NULL DEFAULT 1,
                frequency_hours INTEGER,
                dose_time TEXT NOT NULL DEFAULT '08:00',
                total_quantity INTEGER NOT NULL CHECK (total_quantity > 0),
                remaining_quantity INTEGER NOT NULL
                    CHECK (remaining_quantity >= 0 AND remaining_quantity <= total_quantity),
                level_pct REAL GENERATED ALWAYS AS (
                    ROUND((CAST(remaining_quantity AS REAL) / total_quantity) * 100, 1)
                ) STORED
            );

            CREATE TABLE IF NOT EXISTS log (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                slot INTEGER NOT NULL,
                medication TEXT NOT NULL,
                event_time INTEGER NOT NULL,
                status INTEGER NOT NULL
            );

            CREATE TABLE IF NOT EXISTS settings (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );
            """
        )
        columns = {
            row["name"]
            for row in connection.execute("PRAGMA table_info(medications)").fetchall()
        }
        if "dose_time" not in columns:
            connection.execute(
                "ALTER TABLE medications ADD COLUMN dose_time TEXT NOT NULL DEFAULT '08:00'"
            )
        _seed_defaults(connection)
        connection.commit()
    finally:
        connection.close()


def _seed_defaults(connection: sqlite3.Connection) -> None:
    defaults = {
        "volume": "70",
        "refill_alert": "1",
        "wifi": "1",
        "wifi_ssid": "Home network",
        "focus_blink": "1",
        "navigation_beep": "0",
    }
    for key, value in defaults.items():
        connection.execute(
            "INSERT OR IGNORE INTO settings (key, value) VALUES (?, ?)",
            (key, value),
        )


def list_medications(path: Path = DB_PATH) -> list[Medication]:
    connection = connect(path)
    try:
        rows = connection.execute(
            """
            SELECT id, name, strength, slot, active, dose_qty,
                   frequency_hours, dose_time, total_quantity, remaining_quantity,
                   ROUND((CAST(remaining_quantity AS REAL) / MAX(total_quantity, 1)) * 100, 1) AS level_pct,
                   0 AS taken_today
            FROM medications
            ORDER BY slot
            """
        ).fetchall()
        return [Medication(**dict(row)) for row in rows]
    finally:
        connection.close()


def list_scheduled_medications(
    selected_date: date,
    path: Path = DB_PATH,
) -> list[Medication]:
    """Return medications scheduled for a date and completion for that date."""
    connection = connect(path)
    try:
        rows = connection.execute(
            """
            SELECT m.id, m.name, m.strength, m.slot, m.active, m.dose_qty,
                   m.frequency_hours, m.dose_time, m.total_quantity, m.remaining_quantity,
                   ROUND(
                       (CAST(m.remaining_quantity AS REAL) / MAX(m.total_quantity, 1)) * 100,
                       1
                   ) AS level_pct,
                   CASE
                       WHEN COUNT(l.id) > 0
                       THEN MIN(CASE WHEN l.status = 1 THEN 1 ELSE 0 END)
                       ELSE 0
                   END AS taken_today
            FROM medications AS m
            LEFT JOIN log AS l ON l.slot = m.slot
                AND date(datetime(l.event_time, 'unixepoch', 'localtime')) = ?
            WHERE (
                l.id IS NOT NULL
                OR (
                    date(?) > date('now', 'localtime')
                    AND m.active = 1
                    AND m.frequency_hours IS NOT NULL
                )
            )
            GROUP BY m.id, m.name, m.strength, m.slot, m.active, m.dose_qty,
                     m.frequency_hours, m.dose_time, m.total_quantity, m.remaining_quantity
            ORDER BY m.slot
            """
            ,
            (selected_date.isoformat(), selected_date.isoformat()),
        ).fetchall()
        return [Medication(**dict(row)) for row in rows]
    finally:
        connection.close()


def weekly_adherence(
    path: Path = DB_PATH,
    reference_date: date | None = None,
) -> list[float]:
    """Return taken/total ratios for the current Monday-to-Sunday week."""
    details = weekly_adherence_details(path, reference_date)
    return [
        (float(d["taken"]) / float(d["scheduled"])) if d["scheduled"] > 0 else 0.0
        for d in details
    ]


def weekly_adherence_details(
    path: Path = DB_PATH,
    reference_date: date | None = None,
) -> list[dict]:
    """Return adherence details for the week containing ``reference_date``."""
    today = datetime.now().date()
    week_date = reference_date or today
    monday = week_date - timedelta(days=week_date.weekday())
    following_monday = monday + timedelta(days=7)
    connection = connect(path)
    try:
        rows = connection.execute(
            """
            SELECT date(datetime(event_time, 'unixepoch', 'localtime')) AS local_day,
                   COUNT(*) AS scheduled,
                   SUM(CASE WHEN status = 1 THEN 1 ELSE 0 END) AS taken
            FROM log
            WHERE date(datetime(event_time, 'unixepoch', 'localtime')) >= ?
              AND date(datetime(event_time, 'unixepoch', 'localtime')) < ?
            GROUP BY local_day
            """,
            (monday.isoformat(), following_monday.isoformat()),
        ).fetchall()
        by_day = {date.fromisoformat(row["local_day"]): row for row in rows}
        result: list[dict] = []
        for idx, day_name in enumerate(DAYS_OF_WEEK):
            row = by_day.get(monday + timedelta(days=idx))
            scheduled = int(row["scheduled"] or 0) if row else 0
            taken = int(row["taken"] or 0) if row else 0
            result.append(
                {
                    "day": day_name,
                    "scheduled": scheduled,
                    "taken": taken,
                    "is_today": monday + timedelta(days=idx) == today,
                }
            )
        return result
    finally:
        connection.close()


def inventory_chart_data(
    path: Path = DB_PATH,
    *,
    active_only: bool = False,
    limit: int = 8,
) -> list[dict]:
    """Return canister inventory dicts from `medications` formatted for
    `MedicationVolumeChart.set_inventory_data()`.
    """
    connection = connect(path)
    try:
        where_clause = "WHERE active = 1" if active_only else ""
        rows = connection.execute(
            f"""
            SELECT slot, name, strength, active,
                   remaining_quantity, total_quantity,
                   ROUND((CAST(remaining_quantity AS REAL) / MAX(total_quantity, 1)) * 100, 1) AS level_pct
            FROM medications
            {where_clause}
            ORDER BY slot
            LIMIT ?
            """,
            (limit,),
        ).fetchall()
        items: list[dict] = []
        for row in rows:
            strength_str = f" {row['strength']}" if row["strength"] else ""
            label = f"Slot {row['slot']}: {row['name']}{strength_str}"
            items.append(
                {
                    "name": label,
                    "current": int(row["remaining_quantity"]),
                    "max": int(row["total_quantity"]),
                    "unit": "pills",
                    "slot": int(row["slot"]),
                    "active": int(row["active"]),
                    "level_pct": float(row["level_pct"] or 0.0),
                }
            )
        return items
    finally:
        connection.close()


def upsert_medication(medication: Medication, path: Path = DB_PATH) -> None:
    if medication.total_quantity <= 0:
        raise ValueError("total_quantity must be positive")
    if not 0 <= medication.remaining_quantity <= medication.total_quantity:
        raise ValueError("remaining_quantity must be between zero and total_quantity")

    connection = connect(path)
    try:
        if medication.id is None:
            connection.execute(
                """
                INSERT INTO medications
                    (name, strength, slot, active, dose_qty, frequency_hours,
                     dose_time, total_quantity, remaining_quantity)
                VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
                """,
                (
                    medication.name,
                    medication.strength,
                    medication.slot,
                    medication.active,
                    medication.dose_qty,
                    medication.frequency_hours,
                    medication.dose_time,
                    medication.total_quantity,
                    medication.remaining_quantity,
                ),
            )
        else:
            connection.execute(
                """
                UPDATE medications
                SET name = ?, strength = ?, slot = ?, active = ?, dose_qty = ?,
                    frequency_hours = ?, dose_time = ?, total_quantity = ?, remaining_quantity = ?
                WHERE id = ?
                """,
                (
                    medication.name,
                    medication.strength,
                    medication.slot,
                    medication.active,
                    medication.dose_qty,
                    medication.frequency_hours,
                    medication.dose_time,
                    medication.total_quantity,
                    medication.remaining_quantity,
                    medication.id,
                ),
            )
        connection.commit()
    finally:
        connection.close()


def delete_medication(slot: int, path: Path = DB_PATH) -> None:
    connection = connect(path)
    try:
        connection.execute("DELETE FROM medications WHERE slot = ?", (slot,))
        connection.commit()
    finally:
        connection.close()


def refill_medication(slot: int, path: Path = DB_PATH) -> None:
    connection = connect(path)
    try:
        connection.execute(
            """
            UPDATE medications
            SET remaining_quantity = total_quantity
            WHERE slot = ?
            """,
            (slot,),
        )
        connection.commit()
    finally:
        connection.close()


def fetch_setting(key: str, default: str = "") -> str:
    connection = connect()
    try:
        row = connection.execute("SELECT value FROM settings WHERE key = ?", (key,)).fetchone()
        return default if row is None else str(row["value"])
    finally:
        connection.close()


def set_setting(key: str, value: str) -> None:
    connection = connect()
    try:
        connection.execute(
            "INSERT INTO settings (key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            (key, value),
        )
        connection.commit()
    finally:
        connection.close()
