-- Schema reference for Pillba SQLite database (pillba.sqlite3)
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

CREATE TABLE IF NOT EXISTS medications (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,                         -- "AMOX-CLAV"
    strength TEXT,                              -- "125MG" (label dosage strength)
    slot INTEGER NOT NULL UNIQUE,               -- Physical slot/carousel bin number
    active INTEGER NOT NULL DEFAULT 1,          -- 1 = active, 0 = inactive
    dose_qty INTEGER NOT NULL DEFAULT 1,        -- "Take 1" (count per event)
    frequency_hours INTEGER,                    -- "every 12 hours" (12)
    total_quantity INTEGER NOT NULL CHECK (total_quantity > 0), -- "qty: 14" (from bottle label)
    remaining_quantity INTEGER NOT NULL
        CHECK (remaining_quantity >= 0 AND remaining_quantity <= total_quantity),

    -- Auto-calculated percentage (0.0 to 100.0%)
    level_pct REAL GENERATED ALWAYS AS (
        ROUND((CAST(remaining_quantity AS REAL) / total_quantity) * 100, 1)
    ) STORED
);

-- Reset existing example rows
DELETE FROM medications;
DELETE FROM log;
DELETE FROM sqlite_sequence WHERE name IN ('medications', 'log');

-- Seed example medications across Slots 1-8 (normal >25%, warning <=25%, critical <=10%)
INSERT INTO medications (id, name, strength, slot, active, dose_qty, frequency_hours, total_quantity, remaining_quantity) VALUES
    (1, 'AMOX-CLAV',    '125MG', 1, 1, 1, 12, 14,  3),  -- 21.4% (Warning)
    (2, 'METFORMIN',    '500MG', 2, 1, 1, 12, 60, 48),  -- 80.0% (Normal)
    (3, 'LISINOPRIL',   '10MG',  3, 1, 1, 24, 30, 24),  -- 80.0% (Normal)
    (4, 'ATORVASTATIN', '20MG',  4, 1, 1, 24, 30,  6),  -- 20.0% (Warning)
    (5, 'ASPIRIN',      '81MG',  5, 1, 1, 24, 30,  2),  --  6.7% (Critical)
    (6, 'OMEPRAZOLE',   '40MG',  6, 1, 1, 24, 30, 27),  -- 90.0% (Normal)
    (7, 'AMLODIPINE',   '5MG',   7, 1, 1, 24, 30, 16),  -- 53.3% (Normal)
    (8, 'GABAPENTIN',   '300MG', 8, 0, 2,  8, 90,  9);  -- 10.0% (Critical, inactive)

-- Seed weekly adherence log entries relative to Monday 00:00 localtime of the current week
-- status: 1 = taken, 0 = missed / pending
WITH week_base AS (
    SELECT CAST(
        strftime('%s', datetime('now', 'localtime', 'weekday 0', '-6 days', 'start of day'), 'utc')
        AS INTEGER
    ) AS monday_ts
)
INSERT INTO log (slot, medication, event_time, status)
SELECT slot, medication, (SELECT monday_ts FROM week_base) + offset_sec, status
FROM (
    -- Monday (day 0): 4/4 taken (100%)
    SELECT 1 AS slot, 'AMOX-CLAV'    AS medication, 0 * 86400 +  8 * 3600 AS offset_sec, 1 AS status UNION ALL
    SELECT 2,         'METFORMIN',                  0 * 86400 +  9 * 3600,               1 UNION ALL
    SELECT 3,         'LISINOPRIL',                 0 * 86400 + 13 * 3600,               1 UNION ALL
    SELECT 1,         'AMOX-CLAV',                  0 * 86400 + 20 * 3600,               1 UNION ALL

    -- Tuesday (day 1): 3/4 taken (75%)
    SELECT 1,         'AMOX-CLAV',                  1 * 86400 +  8 * 3600,               1 UNION ALL
    SELECT 2,         'METFORMIN',                  1 * 86400 +  9 * 3600,               1 UNION ALL
    SELECT 3,         'LISINOPRIL',                 1 * 86400 + 13 * 3600,               0 UNION ALL
    SELECT 1,         'AMOX-CLAV',                  1 * 86400 + 20 * 3600,               1 UNION ALL

    -- Wednesday (day 2): 3/3 taken (100%)
    SELECT 1,         'AMOX-CLAV',                  2 * 86400 +  8 * 3600,               1 UNION ALL
    SELECT 2,         'METFORMIN',                  2 * 86400 + 12 * 3600,               1 UNION ALL
    SELECT 4,         'ATORVASTATIN',               2 * 86400 + 21 * 3600,               1 UNION ALL

    -- Thursday (day 3): 2/4 taken (50%)
    SELECT 1,         'AMOX-CLAV',                  3 * 86400 +  8 * 3600,               1 UNION ALL
    SELECT 2,         'METFORMIN',                  3 * 86400 +  9 * 3600,               0 UNION ALL
    SELECT 5,         'ASPIRIN',                    3 * 86400 + 14 * 3600,               1 UNION ALL
    SELECT 1,         'AMOX-CLAV',                  3 * 86400 + 20 * 3600,               0 UNION ALL

    -- Friday (day 4): 3/4 taken (75%)
    SELECT 1,         'AMOX-CLAV',                  4 * 86400 +  8 * 3600,               1 UNION ALL
    SELECT 2,         'METFORMIN',                  4 * 86400 +  9 * 3600,               1 UNION ALL
    SELECT 3,         'LISINOPRIL',                 4 * 86400 + 12 * 3600,               1 UNION ALL
    SELECT 4,         'ATORVASTATIN',               4 * 86400 + 20 * 3600,               0 UNION ALL

    -- Saturday (day 5): 0/2 taken (0% scheduled but not yet taken)
    SELECT 1,         'AMOX-CLAV',                  5 * 86400 +  8 * 3600,               0 UNION ALL
    SELECT 2,         'METFORMIN',                  5 * 86400 + 20 * 3600,               0
    -- Sunday (day 6): 0 scheduled entries (demonstrates neutral unscheduled day)
);

-- Seed default settings
INSERT OR REPLACE INTO settings (key, value) VALUES
    ('volume', '70'),
    ('refill_alert', '1'),
    ('wifi', '1'),
    ('wifi_ssid', 'Home network'),
    ('focus_blink', '1'),
    ('navigation_beep', '0');
