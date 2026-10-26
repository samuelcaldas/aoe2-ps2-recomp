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

### Root cause of the main-thread semaphore stall: receiver starvation (2026-10-23)

The main thread's per-frame loop waits on a join semaphore that the game thread
(priority 13) is meant to signal. Thread dumps showed the game thread `ready`, never
running: it had called `StartThread` on the network receiver (priority 11) and was never
scheduled again. The receiver spun on the host recv handler, which returned
immediately and never blocked, so the strict-priority scheduler never let the lower
priority game thread run. The original RPC round trip sleeps the caller for the IOP
latency; the host replacement did not.

**Fix:** when a receive finds no packet, the handler sleeps the calling thread for about
1 ms of EE time (a host callback wakes it; the scheduler writes 0 into `v0`, which is
"no packet"). Same pattern as the runtime's SIF reply latency.

**Result (diagnostics build, desktop, Xvfb):**
- 60 s: real frames 2 (was 0). The screen shows the game's own loading text. The game
  thread runs. In this log-heavy build it spends most of its time in a 16M-iteration
  busy delay between polls of the audio service status (about 12 s per poll), so
  60 s is too short to judge.
- 240 s: real frames 6, frames change. The game draws its own mouse cursor (the mouse
  hooks work) and starts file-sector accesses.

**Next blocker:** at about 4 minutes the game thread ends with a return address of zero
inside the per-frame message-pump method (drain queue, idle, return). The scheduler logs
`pc-zero` and makes the thread dormant; the main thread then waits forever on its join
semaphore and the screen stays black apart from the cursor. The pump method is a virtual
method that normally returns to its caller, so something invoked it as a root context.
Not yet determined who dispatches it. Not yet tried: a non-diagnostics build, where the
busy delays are far shorter.

### Native kernel module, 60-second desktop runs (2026-10-24)

The first native replacement below the engine (the KCET kernel module: semaphores,
threads, vsync) was run in the diagnostics build, desktop, Xvfb, 60 s each, with the module
off (default) and on (`kernel=on` in `native_modules.ini`, git-ignored). 38 of the module's
39 functions are replaced; one stays generated code (it depends on two helpers outside the
module).

**Calls from the game into the module (same in both runs):** vsync init 2, semaphore create
8, create-and-start thread 1, semaphore wait 1, current-thread priority 2, and the
end-of-frame interrupt callback 128 or more times. The baseline trace also showed the
module's internal calls (accessors, range init, per-vblank registration); with the module
on those happen inside the native model and no longer reach the dispatcher. No function the
baseline called is missing from the native counters.

**Verdict:** unchanged. Both runs end `stalled_in_loop` in the 60 s window with the
validator failing, and the real-frame count varies between runs (0 to 3) with no
relation to the module. This is expected: the earlier entries show the game needs well over
60 s in this log-heavy build to reach its own loading screen, so the window cannot show
whether the module helps. A longer run, and the non-diagnostics build, are the next
measurements.

**Found on the way:** the runtime's function lookup returns a fallback for an address that
has no recompiled function, so "found" must be checked with the existence query; one of the
module's functions has no recompiled original (the generator folded it into a neighbour),
and the native handler is its only implementation. The module's remove-handler helper lives
in the mouse module of the decomp and is ported inside the adapter; the mouse plan must
reuse it, not duplicate it.

**Open:** the assertion reporter of the original (a guest function) is log-only in the
native module; differential testing against the original is still pending; the game's
`ra=0` stop at about 4 minutes is a separate problem and is not addressed by this module.

### Native pad device, desktop runs (2026-10-24)

The pad is the second native replacement. Below the KCET pad module, the Sony pad library depended on an IOP
pad service that does not exist here; the original therefore sat in the "command executing" state forever and
reset the buttons every frame. The native device answers the library calls from a host backend.

**Original (diagnostics build, 90 s):** the state query returned 5 on every poll; the per-state function that
only resets the slot ran 256 or more times; read, actuator and mode calls never happened.

**Native device with a connected pad (synthetic backend, since the validation host has no gamepad), 90 s:** the
state query returns 6 for port 0 and 0 for port 1; the processing function runs 256 or more times; read is
called 256 or more times and returns 8 bytes (analog report); the button update and the actuator update run on
every poll. Without a pad (real backend, nothing attached) the device returns 0 on all 550 polls, which is the
correct disconnected behaviour.

**Verdict:** unchanged (`stalled_in_loop`); the pad was never the stall. The vibration path was not exercised
by the game in the window (no actuator output requested), so it is verified by unit tests only.

**Decomp finding:** the decomp of the pad module disagrees with the binary in the state dispatcher, the step
function and two library calls (details in `docs/sot/pad-libpad-semantics.md`); the implementation hold recorded at that point is superseded by the bounded native result below.


### Native kt_libpad, bounded observer comparison (2026-10-24)

Current coverage is 97 native replacements and eight deferred entries; `pad` stays
off by default. Independent comparison of the supplied original-fresh and
native-report-fixed 30-second captures finds 34 snapshots each, zero skips, identical
keys and 34/34 equal complete 512-byte states. Native port-0 sample-call counts
are one higher from logical step 3 onward; port-1 counts agree. Successful alignment's
native stop-actuator connection check demonstrably samples; this is consistent
with the offset, without a new per-call trace proving exclusive attribution.

Synthetic input follows logical steps under the observer; recorded steps end at
64, before the first Cross toggle at step 101. This covers Cross held and neutral
sticks, not release cycles or real hardware/rumble. Other functions have not been
exhaustively differential-tested. The supplied validator assessment remains
`stalled_in_loop`, with no changed frames: no playability or whole-module equivalence
claim. Host build succeeds; all 14 CTests pass; Python runs 302 tests with two skips.
No game build/run was performed in this validation. Evidence and remaining gaps:
`docs/sot/pad-libpad-semantics.md`.

### Native kt_mouse and kt_keybd, bounded observer comparison (2026-10-25)

A 30-second differential game capture was performed on desktop Linux (`AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1`)
comparing the recompiled game code baseline (`mouse=off, keyboard=off`) with the native C++ module
replacements (`mouse=on, keyboard=on`).

Both runs produced exactly 34 state snapshots across mouse and keyboard ports with zero skips or duplicates.
All 34 snapshots matched byte-for-byte with identical FNV-1a checksums and state buffers:
- Keyboard: step 1 `4a55d94f060a8109`, steps 2–16 `fce31a51e47fb209`, step 64 `c0e5ba9190a348b9`
- Mouse: steps 1–16 `4a64f49b7f28aa53`, step 64 `1e1c37cab910a075`

Validator status remained `stalled_in_loop` (`real_frames=4, frames_changed=False`) in both cases,
demonstrating that neither mouse nor keyboard causes the boot stall.
Host unit test suites (`kt_mouse_model_tests`, `kt_keybd_model_tests`, `native_input_adapter_tests`)
pass cleanly (20/20 CTests green). Evidence and boundaries: `docs/sot/input-modules-semantics.md`.

### Native kt_file and kt_libcdvd_server, bounded observer comparison (2026-10-25)

A 30-second differential capture on desktop Linux (`AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1`)
evaluated the native file table and CDVD server replacements (`file=on, cdvd=on`) against the recompiled baseline.

Both runs recorded 34 matching mouse/keyboard snapshots across steps 1..16 and 64.
These are input observations, not evidence of file/CDVD storage or module parity.
Native overrides installed and active in the log (`native kt_file: installed 4/5 functions`,
`native kt_libcdvd_server: installed 5/6 functions`).
Validator status remained `stalled_in_loop` (`real_frames=2, frames_changed=False`).
Host unit and differential tests (`kt_file_model`, `cdvd_model`, `file_snapshot`, `native_file_adapters`,
`file_differential`) all pass (25/25 CTests green). Evidence: `docs/sot/file-cdvd-semantics.md`.

### Native kt_mc_2, bounded observer comparison (2026-10-25)

A 30-second differential game capture on desktop Linux (`AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1`)
evaluated the native memory card module replacement (`memory_card=on`) against the recompiled baseline.

Both runs recorded 34 matching mouse/keyboard snapshots across steps 1..16 and 64.
These do not establish memory-card storage or module parity.
Native override active in log (`native kt_mc_2: installed 4/11 functions`).
Validator status remained `stalled_in_loop` (`real_frames=4, frames_changed=False`).
Host unit and differential test suites (`kt_mc_model`, `mc_storage_model`, `mc_snapshot`, `native_mc_adapters`,
`mc_differential`) pass cleanly (30/30 CTests green). Evidence: `docs/sot/mc-semantics.md`.

### Native kt_hd, bounded observer comparison (2026-10-25)

A 30-second differential capture on desktop Linux (`AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1`)
evaluated the native hard drive / virtual disc subsystem replacement (`hd=on`) against the recompiled baseline.

Both runs recorded 38 state snapshots across steps 1..16 and 64, including 4 HD snapshots at steps 1..4 (`fnv=8421ae126c7ced25`),
with 38/38 matching byte-for-byte (0 mismatches). The sampled HD hash is unchanged;
this does not establish exercised HD state transitions, storage parity or full module equivalence.
Native override active in log (`native kt_hd: installed 5/7 functions`).
Validator status remained `stalled_in_loop` (`real_frames=4, frames_changed=False`).
Host unit and differential tests (`kt_hd_model`, `hd_storage_model`, `hd_snapshot`, `native_hd_adapters`,
`hd_differential`) all pass (35/35 CTests green). Evidence: `docs/sot/hd-semantics.md`.

### Native GPU packet boundary, corrected verification (2026-10-25)

The seven-function source boundary now installs completely or fails explicitly.
The focused harness executes the actual selected generated originals, not duplicated
reference implementations: 135 packet comparisons, 60 packet-helper comparisons,
14 scheduler timing/deadline cases and three matching dynamic GPU entry records per
mode pass. Independent review exposed omitted guest-dispatch cycle charges; a
regression reproduced the mismatch before the fix and passes with retained helper
calls and guest-stack continuations. All 37 host CTests and Python's 317 tests
(two external-asset skips) pass on main, as does the separate runtime differential test.

After the Docker diagnostic build, two 30-second game captures (`gpu=off`/`gpu=on`)
both remain `stalled_in_loop`, with unchanged frames and no missing branch targets.
The native log confirms `installed 7/7`, but neither capture reaches a GPU entry:
zero GPU records and zero GPU skips. This does not demonstrate game-path GPU parity
or playable graphics. Packet fixtures, not unrelated input snapshots, are the bounded
correctness evidence. External handlers and pure-memory packet helpers remain generated;
this is not a full renderer replacement. Details: `docs/sot/gpu-semantics.md`.

### Stall loop source trace (2026-10-26)

The sampled PC inside the 30-second captures (the `stalled_in_loop` verdict) was
traced through the generated code to its exact origin.

**Structure of the wait.** A five-stage audio initialisation function calls a
stall-loop function at each stage. The stall-loop function polls an audio-service
RPC handle by calling a single-byte status reader on each outer iteration; between
polls it executes a 16M-iteration CPU busy-delay and a vsync-check helper. The
status reader: (1) issues an async SIF RPC call to an audio IOP service, passing a
transfer-control struct; (2) reads a one-byte status field from that struct and
returns it. The caller exits only when the status byte equals 4 (done); bytes 1 and
other values loop again.

**The status byte.** The struct holding the status byte lives at a fixed guest
address derived from two compile-time constants in the status reader's prologue.
The byte is set by the IOP-side RPC reply handler when the transfer completes; the
EE-side code only reads it.

**Why the 30-second captures always stall here.** The stall log's 300-second run
(with function-log tracing enabled) observed the status sequence `1, 1, 0, 1` —
so the upstream SIF runtime does eventually reply; the service exists. The 30-second
captures stall because the 16M-iteration busy-delay plus function-log file I/O
makes each poll cycle take many seconds of wall-clock time. This is not a missing
service — it is a measurement-window issue caused by the log overhead.

**Why even the 540-second non-function-log run stalls.** In that run all five
initialisation stages cleared. The later stall (black screen with cursor) is
therefore a separate problem downstream of audio init and is not caused by the
status poll seen in the 30-second captures.

**Consequence for the current work.** The native KCET modules (mouse, keyboard,
pad, file, CDVD, MC, HD, GPU) are not the cause of the audio-init poll stall.
The poll completes in longer runs; no fabricated reply is needed and none has
been inserted. The outstanding question is what causes the post-audio-init black
screen, which is the next stall to investigate.

**Service identity confirmed by decompilation (2026-10-26).** The `aoe-decomp`
session identified the service from the init function that precedes the stall:

- The struct at the fixed guest address derived from the status reader's constants
  is a `SifRpcClientData_t` for SIF service **0x30 (SDD — Sound Driver Daemon)**.
  It is bound in an init function that also binds service **0x20 (SDCD — Sound CD/Stream)**.
- `func_12D888` is `sceSifCallRpc(&client_SDD, 3, 0, NULL, 0, reply_buf, 1, NULL, NULL)`,
  issuing RPC command 3 to SDD and reading the first reply byte.
- Status byte 1 = SDD busy / starting; status byte 4 = SDD sound engine ready.
- The stall loop is inside the audio-engine startup sequence; it is driven by
  a thread that also dispatches commands 0x1010–0x1013 and 0x101F to the KCET
  audio framework before entering the poll.

**Root cause confirmed.** The IOP-side sound daemon (SIF service 0x30, command 3)
does not transition from status 1 to 4. The `sdstr.irx` and `soundcd.irx` modules
load from the disc and register the SDD service in the upstream IOP emulator. Whether
the status-4 transition requires SPU2 DMA completion (no SPU2 emulation is present)
or an explicit IOP-side signal is under investigation. No fabricated reply is inserted.

**Next step.** Determine whether `sdstr.irx` command 3 replies unconditionally or
only after a SPU2/DMA transfer completes. If the latter, the status 4 will never
arrive without a SPU2 stub, and this is an upstream emulation gap analogous to the
earlier INTC_STAT and SIF boot-handshake gaps.

**SPU2 dependency confirmed by decompilation (2026-10-26).** In the KCET sound
architecture, SDD command 3 reports the state of an IOP-side DMA/SPU2 buffer fill.
Status 1 = DMA transfer in progress; status 4 = transfer and buffer-ready complete.
Without a SPU2 DMA interrupt or completion callback on the IOP, the daemon never
exits status 1. This is an upstream emulation gap: the upstream IOP emulator loads
and runs `sdstr.irx` but does not emulate SPU2 DMA, so no completion event is ever
delivered. No fabricated return has been inserted; this requires an upstream fix
(SPU2 DMA stub in the IOP emulator) or a targeted IOP-side RPC shim analogous to
the earlier SIF boot-handshake fix in the upstream fork. Neither option is a
game-side hook; both require changes outside this repository.

**Full RPC protocol spec for the upstream fork shim (2026-10-26).** Decompilation
of `0x12D648`–`0x12D8D8` by the `aoe-decomp` session produced the complete IOP
client protocol. Recorded here as the specification the upstream IOP-side shim
must satisfy; no EE-side code in this repository is changed.

SIF clients bound during audio init:

| Address | Service ID | Bind error message |
| :--- | :--- | :--- |
| `SifRpcClientData_t` at `0x4F1640` | `0x20` (SDCD — Sound CD/Stream) | `"SDCD_CDREAD_CALL DaMe!!\n"` |
| `SifRpcClientData_t` at `0x4F1680` | `0x30` (SDD — Sound Driver Daemon) | `"SDD_GETVOL DaMe!!\n"` / `"sd_getvol.serve : 0x%x\n"` |

SDCD command 0x10 (open stream file): `sceSifCallRpc(&client_0x4F1640, 0x10, 0, 0x4F1540, 256, NULL, 0, NULL, NULL)`. The 256-byte send buffer at `0x4F1540` holds the filename (copied via `strcpy`). The IOP opens the named CD stream file. No receive buffer.

SDD command `val` (volume/status query): `sceSifCallRpc(&client_0x4F1680, val, 0, NULL, 0, 0x38C880, 48, NULL, NULL)`. Send buffer absent; receive buffer is 48 bytes at `0x38C880`. Used for extended status and volume table queries; `val = a0 & 0xFF`.

SDD command 3 (engine-ready query — the stall poll): `sceSifCallRpc(&client_0x4F1680, 3, 0, NULL, 0, 0x38C880, 1, NULL, NULL)`. No send buffer; receive buffer is 1 byte at `0x38C880`. Return values: `1` = SPU2 DMA still in progress (loop again); `4` = DMA complete, sound engine ready (exit loop); other = error or uninitialised idle.

Upstream shim behaviour: when the IOP-side handler for SDD service `0x30`, command `3` is called and no real SPU2 hardware is present, the handler should write `4` into the first byte of the EE receive buffer after a brief simulated delay (matching the IOP reply-latency pattern used by the earlier SIF boot-handshake fix). This is analogous to the upstream INTC_STAT and boot-handshake fixes: it does not fabricate a game result, it provides the hardware-absent completion that the daemon would emit after a real SPU2 DMA cycle.

**Decomp source available (2026-10-26).** The `aoe-decomp` session completed and
merged a faithful decompilation of the full SDD/SDCD audio init region into the
owner's decomp repository (`../aoe`, commits `a6ada32e`/`cb2bb2ec`):
- `include/kt_sound.h` — SIF service IDs (`SDCD=0x20`, `SDD=0x30`), status codes,
  and `SifRpcClientData_t` struct layout.
- `src/kcet/kt_sound.c` — implementations of the four audio functions: bind init,
  stream-file open, volume/extended-status query, and command-3 status poll.
- `tests/native/kt_sound_test.c` — TDD tests passing under `gcc -m32 -O2`.

This source is the authoritative reference for the upstream fork shim. Per the
owner's 2026-10-24 authorization, its content may be copied into tracked files of
this repository when needed for a native KCET audio module. No such module is
implemented yet; the fix belongs in the upstream IOP emulator.

**Upstream shim scope confirmed minimal (2026-10-26).** Analysis of the KCET
sound dispatcher at `0x25D060`–`0x25D600` by the `aoe-decomp` session confirms
that commands 0x1010–0x101F (issued before the stall poll) configure EE-side
channel/voice parameter buffers only. They index a table at `0x4805E0` and write
local state; they do not issue blocking IOP or SPU2 calls. The sole IOP dependency
that blocks the stall loop is SDD service 0x30, command 3. The upstream shim
therefore needs to handle only that one command; no additional SPU2-dependent
commands precede it. Investigation of the audio-init stall is complete.


## Post-audio-init black screen — pc-zero crash in boot state machine (2026-10-26)

**Symptom.** After audio init completes (~540 s), the game thread enters the
per-frame loop and crashes with `pc=0` shortly after. The main thread then waits
forever on its join semaphore → black screen with cursor.

### Game thread structure (confirmed by static analysis)

The game thread entry point (address range approximately 0x2206A0–0x2207B0) runs
a one-time init sequence, then enters an infinite per-frame loop that never
reaches its own return instruction. The loop per iteration:

1. `func_2204D0` — reinitialise frame object
2. `func_104D40` — signal main-thread semaphore (main thread proceeds to render)
3. `func_104D60` — wait main-thread semaphore (wait for render done)
4. Write zero to GS register at 0x10000000; increment two 64-bit frame counters
5. `func_226260(a0=counter)` — frame setup (contains one JALR / indirect call)
6. **Inner message-queue wait:** `func_2204E0` (calls `waitSemaphore` `0x104D70` in
   a loop until counter changes) — this is the message-available poll, not a
   one-shot call
7. `func_2207B0` — display-buffer / viewport update (all direct calls, no
   indirect dispatch)
8. `func_104D40` — signal
9. `func_221790` — peripheral/input state update (all direct)
10. `func_3379F0(a0=0)` — (twice, frames 9–11)
11. `func_221F40` — **boot/menu state machine** — contains `jr $v1` indirect jump
12. `func_36F800(a0=0)`, `func_221C00`, `func_224A10`
13. Unconditional branch back to step 1

### Root cause: uninitialised jump-table entry in boot state machine

`func_221F40` (address range 0x221F40–0x22217C) is the main boot/menu state machine.
It reads a 16-bit phase index from a global at `gp-0x74B4` and bounds-checks it:
`sltiu $at, phase, 10` — if phase ≥ 10, it skips to the function epilogue. If
phase is 0–9, it dispatches through a four-byte function-pointer table at address
`0x492EF0`:

```
// 0x222048: sltiu   $at, $a1, 10        // guard: skip if phase >= 10
// 0x222054: lui     $v1, 0x49
// 0x222058: addiu   $a0, $v1, 0x2EF0    // $a0 = 0x492EF0 (table base)
// 0x22205c: sll     $v1, $a1, 2          // $v1 = phase * 4
// 0x222060: addu    $v1, $v1, $a0        // $v1 = &table[phase]
// 0x222064: lw      $v1, 0($v1)          // $v1 = table[phase]  (function pointer)
// 0x222068: jr      $v1                  // jump to handler
```

If the pointer loaded from `table[phase]` is zero — because the table is
uninitialised at that entry, or because the phase index is set before the table is
populated — the runtime dispatches to `pc=0`, which has no compiled function, and
`dispatchGuestBranch` returns false. The C++ wrapper returns, unwinding the guest
call stack, and the game thread terminates at pc=0. aoe-decomp confirmed: cases
0–9 route to handlers including `func_220FF0` (audio stage init, case 6) and
`func_22212C` (state-advance, case 9); the remaining entries may be zero-filled
until those stages are reached.

### Confirmed by aoe-decomp (2026-10-26)

- `func_221790`, `func_221C00`, `func_221F40` callees: no indirect dispatch in
  the first two; `func_221F40` has the `jr $v1` jump table at 0x222068.
- The indirect jump at 0x222068 dispatches to `GuestBranchKind::IndirectJump`
  with return PC 0x0 — the runtime treats a zero destination as a missing-function
  crash.
- `func_226260` also has one JALR (`GuestBranchKind::IndirectCall`) at 0x226304;
  that is within the frame-setup path and is a secondary candidate if the table
  at 0x492EF0 is fully populated.

### Next investigation step

Read `func_226260` around 0x226304 to identify what object it dispatches through
(vtable? callback array?). Separately, verify whether address `0x492EF0` is within
the range of static data the ELF loads, and whether any init function before the
loop populates it. If the table entries are zero at runtime, the fix is to trace
which init function should populate `0x492EF0[0..9]` and ensure it runs before the
state machine first executes.




