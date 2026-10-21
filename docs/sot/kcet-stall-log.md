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

## Cause: SIF boot handshake, not an input device

Traced from the diagnostic run's SIF events and the guest code around the spin:

1. The guest reads the SIF boot-status register (ready in the runtime), sets
   the main and sub address registers, then reads the MsCom register
   (`0x80000002`). The runtime seeds MsCom to 0 and nothing updates it.
2. The guest sends a 16-byte SIF DMA packet to the IOP whose first argument
   is the MsCom id `0x80000002`.
3. The guest then polls a reply word in guest memory until it becomes nonzero.
   Only an IOP-side reply would write it. The runtime has no IOP side for this
   handshake, so the poll never ends. The spin is the wait loop itself.

The keyboard, pad, and memory-card services are never reached: the guest is
still inside boot. Adding input hooks now would not change the stall.

Upstream (`third_party/PS2Recomp`, not editable here) seeds the handshake
registers and has no IOP reply path for MsCom, so this is an upstream
emulation gap. Options, in the order to evaluate them:

- Ask upstream to add the IOP boot reply for MsCom, then re-run the baseline.
- Or an explicit game-side handshake that the IOP would perform, written so it
  is traceable and does not fabricate any game result. This must be discussed
  before it is implemented, because the plan rejects emulating lower layers.

A hook that returns a made-up result for the reply is not allowed.

## Progress after porting Play!'s SIF command model (fork, 2026-10-21)

The handshake gap was closed in the PS2Recomp fork (not yet pushed) by an IOP
side of the SIF command protocol modeled on Play!'s `CSIF` (BSD 2-Clause), not
by a game-side hook. Each step below has a fork unit test.

| Step | Fix in the fork | Result in the diagnostic run |
| :--- | :--- | :--- |
| SIF command endpoint | INIT/CHANGE_SADDR, SET_SREG reply, BIND/CALL/OTHERDATA answered with RPC_END into the EE receive buffer, one packet per SIF0 interrupt | Boot handshake completes; the RPC semaphore is signalled |
| LOADFILE server | Module loads routed to the upstream IOP emulator | The disc's IRX modules load and run on the IOP emulator |
| CDVDFSV server | Init, disk type, status, read, search, stream, read-chain over the virtual disc | CD init and sector reads succeed |
| IOP reboot | RESET packet completes the reboot on the next SMFLAG poll | The EE re-runs the handshake after the reboot |
| Callback stacks | Interrupt and callback stacks are carved below the main thread stack | The main function no longer returns to address 0 |
| DECI2 without a host | Requests fail in non-debug builds | The game disables its debug TTY instead of waiting |
| Semaphores | `max_count` 0 means no maximum | A kernel-layer assertion no longer fires |
| StartThread | Returns the thread id | A kernel-layer assertion no longer fires |
| IOP latency | Replies reach the EE after ~55 us of EE time | Lower-priority threads run between RPC polls |
| Disk type | PS2 CD for a disc that fits on a CD-ROM | The game accepts the disc |

Current state: the game draws its own loading screen (real frames, not the
runtime fallback), but the frame stops changing after a few seconds. The
validator still fails on "frames change". Investigation of the remaining
stall is in progress.

## Cause of the zeroed data table: disc reads without a disc image

Watching writes to the table that holds the game's handler pointers showed
it being overwritten with zeros by a block copy whose source is a CD read
buffer. Every disc read in the diagnostic run fails: the game asks for sectors
by their real disc LBN, and the runtime has no disc image configured
(`cdImage` is empty), so `sceCdRead` reports an unresolved LBN and returns
without filling the buffer. The copy then moves the buffer's zero contents
into the table.

Fix direction: configure the owned disc image as the runtime's CD image so
real LBNs resolve. The image must match the recorded disc hash, and it stays
outside the repository.

With the owned disc image configured, every disc read succeeds and the table
is no longer zeroed.

## Cause of the next stop: VSync wait never blocked (INTC_STAT not emulated)

The run then stopped when a worker thread returned to an address that was a
GS display register value. Watching that thread's stack showed GS packet data
written over its saved return address: one graphics packet buffer had grown
megabytes past its end, through static data, into the stack.

The packet is reset once per frame by a VBlank-driven task. The loading
screen's fade loop draws one packet per iteration and waits for the next
VSync with the Sony graphics library's sync routine, which clears the
VBLANK_START bit in the INTC status register and polls it until the next
VBlank sets it again. The runtime stored INTC_STAT as a plain value, so the
cleared bit read back as set and the wait returned at once. The fade loop then
ran thousands of iterations per frame and overflowed the packet buffer.

Fix in the fork: INTC_STAT is write-one-to-clear, and every INTC interrupt the
scheduler dispatches latches its status bit, even when the cause is masked or
has no handler. Two fork tests cover it.

### Desktop validation after the INTC_STAT change

The subsequent 90-second diagnostic run remains **FAIL**: the process stays
alive, there are no missing branch targets, and the loading screen is visible,
but the validator reports `stalled_in_loop`, one real screenshot, and no frame
change. The earlier invalid-target stop did not recur during this run; this
alone does not prove that every packet-buffer overrun has been eliminated.

The sampled guest PC is now inside a bounded delay within a startup status
poll, rather than the earlier packet-append path. The main thread still waits
on its original semaphore. No SIF CDVD reads are logged in this run, so it
provides no additional disc-read coverage. The meaning and producer of the
polled startup status are under investigation; no success response or semaphore
signal has been fabricated to bypass it.

An unchanged-binary run extended to 300 seconds also fails validation: the
process remains alive with zero missing targets and four real screenshots, but
no screenshot change. Four startup status replies are observed, in sequence
`1, 1, 0, 1`; therefore a permanently unchanged status is ruled out. The request
uses a separate audio-side RPC service, not the controller service. Inspection
of the owned audio module confirms that it registers this service and derives
its reply from pending transfer and stream state. The long guest delay between
queries contributes to the wall-clock wait; a missing completion path is not
yet established. The local full log is retained outside the repository.
