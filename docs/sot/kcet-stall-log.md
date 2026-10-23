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

Inspection of the owned caller shows five sequential load-and-wait stages using
this same status helper. In the 300-second log, a command for the second stage
follows the zero status reply. Thus the run did make internal startup progress,
although it failed the visual-progress criterion. Repeated samples within this
bounded delay are not sufficient to prove a permanent deadlock.

### Strict mouse/keyboard registry validation

A subsequent 180-second run of the linked registry build confirms that all ten
configured mouse/keyboard replacements apply, with zero unknown hooks and no
pad aliases configured. Its log has zero missing branch targets and two audio
status replies, both busy; the sampled PC remains in the bounded delay.

The validator then raises `subprocess.TimeoutExpired` while waiting for the game
to stop after termination. It exits before producing its final verdict or
analysing the screenshots. This is a validator cleanup failure, not proof of a
game crash, successful boot, or successful input. No screenshot files from this
run were present when inspected afterward; visual results cannot be certified
from that run. A separate Xvfb screenshot smoke test succeeds.

The validator now terminates and waits for each child with a bounded timeout,
escalates to kill on timeout, and reaps it with another bounded wait. Both the
game and Xvfb are cleaned up on exceptional paths. Six regression tests cover
these cases; the complete Python suite passes (186 tests, one skipped), host
CTest passes all five tests, and the existing fork test executable passes all
483 runtime tests.

A fresh 300-second run produces a complete **FAIL** report without the shutdown
exception: execution started, the process remained alive, four captured frames
are real but unchanged, and zero missing targets are recorded. All ten input
hooks apply; pad aliases remain absent. Audio status replies are
`1, 1, 0, 1, 0, 1`, with successive commands reaching the third of the five
startup loading stages. The menu is still not reached. This is evidence of
internal progress, not working gameplay or verified keyboard/mouse interaction.

Further inspection of the owned audio support library identifies the relevant
request/status pair as enqueueing and checking tickets in a bounded CD-read
queue. It is not a direct SIF DMA completion query. The queue does advance in
the observed runs; no defect in its completion path has yet been demonstrated.

### Function-trace output experiment

The diagnostic build flushes its function-entry/exit file after each record,
producing gigabytes during a run. A second 300-second experiment uses the same
executable from a temporary working directory with only that trace file linked
to `/dev/null`; runtime, RPC, TTY, and error output remain captured normally.
This is an experimental output-sink change, not a guest-code change or a
permanent runtime fix.

With this sink, all five startup loading commands are issued and each wait
observes a clear status. All ten input hooks apply, and 23 SIF disc reads succeed
with zero failures. No missing targets or fatal messages are recorded. The
loading text disappears; the final captured screen is black with a visible
cursor, not a verified menu. The changed output sink is associated with much
more internal progress than the file-writing run, but does not establish the
remaining boot/rendering cause or correct input behavior.

Validation is still **FAIL**: `stalled_in_loop`, one frame classified as real,
and no differing pair of real frames. Most captures are classified as low
detail. The transition must not be called a gameplay pass or hidden by relaxing
the frame classifier. CPU profiling was unavailable because the host restricts
`perf`; no host permission or kernel settings were changed.

### Extended run and callable hook boundaries

A 540-second run using the same trace-output sink still fails validation:
seven captures classify as real and the series changes, but the final screen
is black with a cursor rather than a menu. All five audio waits clear; all
23 SIF disc reads succeed; no missing targets are recorded. This rules out
calling the earlier 300-second screen a proven permanent audio wait, but does
not establish the cause of the later screen.

Checking the hook file against callable boundaries uncovers a separate concrete
error: three keyboard roles resolve to internal continuation labels inside
larger functions. The runtime's successful replacement count only proves that
those dispatch addresses were accepted, not that the intended function entry
was intercepted. Earlier 10/10 reports are therefore not input validation.

The resolver now excludes continuation labels from callable boundary lookup and
rejects them as hook starts. Two additional wrappers require a full client
address constructed into a different destination register after intervening
instructions; this form is accepted only while the source register remains
valid and without crossing control flow. Synthetic regressions demonstrate the
old failures, reject overwritten-register decoys, and preserve callable named
functions and thunks. The focused resolver suite passes 32 tests, and all ten
roles resolve from the owned ELF into the ignored local hook file. No game
addresses or generated sources are changed in tracked files.

The prior extended log also shows repeated unhandled requests from the keyboard
client using an uninitialized service value. A controlled 300-second run with
the corrected hook file and the unchanged executable records zero such calls.
It still fails menu validation: the loading screen remains, only two audio
waits clear in that window, no additional SIF disc reads occur, and no missing
targets are recorded. Correct interception is improved; boot success is not
established by this result.

### Separating function tracing from runtime diagnostics

The fork now exposes `PS2X_ENABLE_FUNCTION_LOG_TRACKER`, defaulting to enabled
for compatibility. The desktop/Switch build commands explicitly disable it,
while retaining runtime, aggressive, and RPC diagnostics as well as the
stop-on-missing-target override in the desktop diagnostic build. A functional
build-command regression covers the flag. The configured Ninja rules confirm
that generated units retain diagnostic definitions but omit the function
tracker; this requires recompiling the generated units, not editing them.
Runtime menu validation with this build remains pending until it links and runs.

### Startup-state investigation (2026-10-22)

This section records the read-only observation sessions after the function-trace
experiment. It contains no guest addresses or binary symbol names, per the
provenance rules; the detailed evidence stays in local files outside the repository.

**Breakpoint on the audio-status poll.** Watching the function that contains the
audio-status service request entered only a small number of times during a 300-second
run. Its status word changed from zero to a nonzero packet-like value on a later
entry. That function is a bind/registration initializer, not the repeating poller.

**Repeating poll.** The repeating service requests came from the socket receive
wrapper, not from the initializer. Over 500 seconds, about 206,000 requests were
issued from one caller in the receive path. The decompilation reference states that
the receive loop repeats whenever the packet length is not the expected size, and
that a missing peer does not block the main startup transition.

**Data-transfer wait.** The startup state machine waits on the low busy bits of the
data-transfer flag. In one observed run that byte changed 17 times and then stayed at
zero for the rest of the window, so that wait completed. A follow-up watch on the startup
stage and outer state did not record any change, and the cause of that
silence is not established. The result is inconclusive for stage progression.

**Network errors.** The terminal log records socket bind and connect failures with
error -2000 for the local, broadcast, and receive endpoints, after which the receive
thread starts anyway. These are real errors but do not by themselves account for the
stall.

**PC reference.** The local PC trial build (1999) uses DirectPlay for networking and
is outside the target editions in the decompilation ADR, so no matching reference exists yet.

**Status.** The stall cause is not identified. No hook, status value, or semaphore has
been forced. The next measurement compares the state after the data-transfer wait
clears with the hot loops observed in the same window.

**Reference plan.** A decompilation plan for the keyboard, mouse, and network layers
exists in the reference project's agent workspace. It covers the mouse unit (already
registered), the keyboard units (not yet split from the shared blob), and the network
helpers that are still unsplit. The network initializer and send wrapper are already
matched there. The plan's verification gates are byte-level matches against the owned
ELF, which the repository's provenance rules keep out of tracked files.

**Matched network code and the discovery loop.** The reference project already has
matched sources for the transport initializer and the receiver thread. Together they
explain the earlier observations. The initializer opens three sockets, binds and
connects each one at most five times, logs each failed attempt, and continues
without aborting. The receiver then runs in discovery mode, polling with a receive
call on every iteration until a connection is established. The -2000 bind and connect
lines are therefore that retry path failing five times. A receiver that never connects
spins on its discovery poll, which matches the repeated receive calls. The reference
sources do not define the -2000 code, so its meaning remains unverified.

**Socket RPCs are never served (60-second probe, 2026-10-22).** The diagnostics
binary was run alone for 60 seconds, without a debugger. Every request to the three
socket-service operations (open, bind, connect) and to a fourth startup request is
reported as handled, yet the reply words are identical to the request words. In each
case the first word is the same error constant the guest wrote before the call (-2000
for the socket operations, -1000 for the fourth), so no service wrote a result. This
matches the TTY lines: the -2000 values are the guest's own pre-set default surviving
an RPC that nobody answered, not an error returned by a server. The receive request
shows the same echo pattern, so its repeated polling never receives a result either.
The bind/connect failures are therefore a missing IOP socket service, not a game bug.

This is an inference from request/reply word equality in the runtime log. It has not
been confirmed against the runtime's RPC dispatch code, and it does not show that
the missing service blocks the loading-to-menu transition.

### Correction: socket services exist but have no hardware (2026-10-22)

The statement above that the bind/connect failures are "a missing IOP socket service"
was wrong. The three socket IRX modules load from the disc and register their RPC
servers; the guest binds all twelve of their clients at init. What is missing is the
FireWire hardware behind them, so a call returns the request unchanged.

The EE-side wrappers each build a request, make one RPC call, and return word 0 of the
reply (`docs/sot/ilink-socket-wrapper-spec.md`). With an echoing reply, word 0 is the
first request word: the socket id for bind/connect/send/recv, and the domain constant
for open. A receive then reports a short or garbage count instead of the 492 bytes the
receiver thread waits for, so it polls forever. The `-2000` values are still the
guest's pre-set default and are not a service error code.

These wrappers are now replaced by a host socket model (`kcet.net.*` hooks). Whether
this changes the boot stall is measured separately below and is not assumed.

### Measurement: host socket model, 60-second desktop run (2026-10-22)

Diagnostics build with `kcet.net.*` applied (17/17 hooks), run by `tools/validate_desktop.py`
for 60 seconds under Xvfb.

- The `-2000` bind/connect lines no longer appear. After init, no socket RPCs are
  issued; the 20 matches of those service ids are the init-time binds only.
- The receiver thread is alive and polls the host model in a tight loop (it keeps
  returning "no packet", as expected with no peer). It no longer receives garbage counts.
- The main thread is unchanged: it waits on the same semaphore for the whole run. The
  screen is still the solid fallback (real frames: 0, verdict: stalled_in_loop).

**Conclusion:** the i.LINK network layer is fixed on its own merits but is **not** the
cause of the boot stall, which matches the earlier static analysis. The stall lies
elsewhere (main-thread semaphore wait). Not yet run: the 500-second window and the
two-process discovery test.
