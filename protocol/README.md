# Shared protocol

[`protocol.md`](protocol.md) is the authoritative Pi–MCU wire contract.
[`test_vectors/`](test_vectors/) contains byte-level examples that both
implementations must satisfy.

The dispense payload is currently a reconciliation gate because the existing
MCU sketches and panel-ui documentation disagree. Do not treat the example
vector as final production approval until both contributors approve the
canonical payload.
