# Collaboration Rules

## Branches and integration

Use `main` for small, independently owned changes. Use a feature branch and
pull request before integrating any change that modifies:

- frame layout, message types, payload fields, CRC, or request IDs;
- shared protocol documentation or test vectors;
- behavior relied on by both Pi and MCU code.

Protocol changes require approval from both contributors. Keep commits small,
explain behavior changes, and include tests and documentation with the change.

## Ownership

| Area | Primary owner | Required review |
| --- | --- | --- |
| Pi Python application | Pi contributor | Firmware review when protocol is affected |
| SQLite schema and repositories | Pi contributor | Pi contributor |
| UART implementation | Pi contributor | Both for contract changes |
| Peripheral drivers | Firmware contributor | Firmware contributor |
| Pin and actuator safety | Firmware contributor | Both when externally observable |
| Protocol specification/vectors | Shared | Both |
| Pi-to-MCU bench integration | Shared | Both |

## Definition of done

A change is complete when:

1. The relevant automated tests pass.
2. Required physical hardware validation passes for hardware behavior.
3. Documentation and shared vectors are updated when behavior changes.
4. The owning contributor and any required cross-boundary reviewer approve it.
5. No unresolved integration issue is hidden as a successful result.

## Legacy code

Legacy source projects are retained read-only under `archive/legacy/`. Generated
`.pio/` output and other build artifacts are not archived. Do not import
archive code from production code; copy behavior intentionally and add tests.

