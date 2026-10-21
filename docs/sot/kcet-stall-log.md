# KCET stall log (diagnostic baseline)

Recorded with `tools/validate_desktop.py --run-seconds 60 --interval 15` on the
desktop diagnostic build (`tools/docker_build.py --platform desktop --diagnostics`).
Addresses and game symbol names are kept out of this tracked file (see
`docs/sot/prior-art-and-reuse.md` §4); the local run log is git-ignored.

## Baseline result (before any new KCET hook)

| Check | Result |
| :--- | :--- |
| Process alive at end of 60 s | yes |
| Guest verdict (`classify_run_log`) | `stalled_in_loop` |
| Program counter over the run | one value, unchanged across ticks |
| Real (non-fallback) frames | 0 |
| Missing-target lines | 0 |
| Unserved RPC trace lines | none recorded |

Reading: the guest is spinning on one instruction address with the return
address unchanged, the GS shows no DMA, GIF, or VIF traffic, and no frame is
presented. The runtime never reports a missing target, so the spin is inside
code that exists, not a call to an absent function.

## Decision table for the next steps

| Candidate cause | Evidence needed | Status |
| :--- | :--- | :--- |
| Waiting for an RPC service that no IOP module serves | unserved-RPC lines with the sid of the service | not observed in this run; the IOP trace did not fire |
| Wait on a semaphore or vsync that the runtime never signals | the spin sits in a kernel wait helper | not yet checked |
| Input device service missing (keyboard, pad, memory card) | RPC binds for those service ids | not yet traced |

The next step is to trace which service the spinning code is waiting on,
before adding any hook. A hook that returns a made-up result is not allowed.
