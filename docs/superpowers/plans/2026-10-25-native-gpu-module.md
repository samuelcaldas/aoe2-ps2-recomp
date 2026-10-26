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
- [x] Build diagnostic game in Docker and capture original/native for 30 seconds each;
  all seven overrides install, but zero GPU calls are reached before the boot stall.
  This is not a game-path parity claim; genuine differential fixtures supply the GPU proof.
- [x] Independent review and RED/GREEN regression for scheduler dispatch charges and
  deadline-visible packet state, with original guest helper calls/continuations retained.

Outside this delivery: all 29 external production handler semantics and full renderer
replacement. The seven-function source boundary is implemented; those external dependencies
remain generated and are explicitly not claimed replaced.

GPU remains off by default. Temporary GPU switches were restored byte-exactly;
no saves, generated/dependency sources or renderer implementation were changed.
The verified boundary was integrated locally into main; no push was performed.
Validation commands and coverage limits: `docs/sot/gpu-semantics.md`.
