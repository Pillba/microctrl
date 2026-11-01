# Pi SQLite Rules

SQLite is owned by the Pi application. One Pi process is the expected database
writer. All schema changes use versioned migrations.

The existing panel-ui database uses these logical tables:

- `medications`: unique physical slot, medication metadata, dose quantity,
  schedule fields, total quantity, and remaining quantity.
- `log`: slot, medication snapshot, Unix event time, and taken/missed status.
- `settings`: string key/value application settings.

The panel-ui reference also enforces positive total quantity, nonnegative
remaining quantity bounded by total quantity, and unique medication slots.
Retain those invariants when extracting the database into `pi/`.

## Test isolation

Tests never use the production database. Use an in-memory database for
repository tests and a temporary file for persistence, migration, locking, or
restart tests. Foreign keys and the same migration path used in production
must be enabled in tests.

## Dispense transaction

Inventory is not decremented when a command is merely sent. The operation
must be represented as pending/reserved first:

```text
create pending operation and reserve quantity
→ send DISPENSE_CMD
→ successful DISPENSE_RESULT: atomically decrement and complete
→ MCU error: resolve without decrement
→ timeout/disconnect: mark UNKNOWN; keep quantity reserved
```

An `UNKNOWN` result is a possible physical success with a lost response. It
must remain visible and block automatic retry. Explicit operator resolution
may confirm dispensed, confirm not dispensed, retry with the same operation
ID, or inspect available sensor evidence. Inventory is finalized only by the
resolution action.

The operation ID must be unique and durable across process restarts. A delayed
response or repeated reconciliation request must not apply inventory changes
twice.

The current panel-ui manual dispense workflow is explicitly a UART
communication test and does not change SQLite inventory. That behavior is
acceptable for the prototype, but production dispense behavior must not claim
inventory success until the transaction rules above are implemented.

## Required database tests

Cover migrations, required-field validation, slot uniqueness, transaction
rollback, successful completion, MCU errors, unknown outcomes, reconciliation,
duplicate operation IDs, and persistence after close/reopen.
