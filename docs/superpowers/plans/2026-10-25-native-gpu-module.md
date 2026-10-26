# Native GPU packet boundary implementation state

The earlier register-map/context-model plan was incorrect. The seven authorized
entries append GIF packets. There is no `LastRegVal` state and no separate native
GS register-map model.

- [x] Read owner decomp contracts and authoritative generated helper behavior.
- [x] Inventory exactly seven actual entries; reject invented trailing entries.
- [x] Implement pure `GuestAccess` tag/payload writers, begin and finalization.
- [x] Dispatch to real runtime guest handlers with guest-stack continuation.
- [x] Fail fast on invalid kinds, missing handlers, and incomplete installation.
- [x] Replace constant-region observer with dynamic packet/context entry snapshots.
- [x] Remove duplicate GPU call-tracer observations.
- [x] Test full packet bytes, cursor/tag state, high bits, selector bits and aliasing.
- [x] Add genuine generated/native differential target, without copied references.
- [x] Exercise independent contexts, nested calls, yields and scheduler resumption.
- [x] Verify nonempty GPU-only snapshot equality and exactly-once entry capture.
- [x] Narrow earlier file/CDVD, memory-card and HD parity claims.
- [ ] Full-game original/native GPU capture (not performed in this task).
- [ ] All 29 external production handler semantics (outside this seven-entry boundary).

GPU remains off by default. No shared game config/save changes, generated/dependency
source edits, renderer implementation, merge, or push are part of this task.
Validation commands and coverage limits: `docs/sot/gpu-semantics.md`.
