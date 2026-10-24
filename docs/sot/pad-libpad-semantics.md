# Pad: what the original libpad path does and what the native device must report

Evidence from the unmodified diagnostics build (desktop, Xvfb, 90 s, no native modules): the call trace
of the KCET pad module and of the Sony pad calls it makes, with the value returned on exit
(`[call-trace] ret ...`). Decomp function names are guest addresses. No binary snippets here.

## Observed

- **State query (`func_0010D2E0`, called from the module's per-slot poll `func_001275A0`):** returns `5`
  (executing a command) in every logged call, for both ports, 256 or more calls. It never leaves 5. The
  guest Sony library waits for an answer from the IOP pad service, which does not exist here, so the
  command never completes.
- **Port setup:** the module's init runs once; the Sony init, the slot-maximum query (ports 0 and 1) and the
  port open (ports 0 and 1, slot 0) are each called, and the module then proceeds to poll. Their return
  values are not in the trace (the calls block on the IOP round trip, so no exit line is written). The
  control flow after them (init result compared with 1, polling only after a successful open) shows that
  init and open returned 1.
- **Calls that never happen:** read, request state, actuator and mode queries, main-mode set, direct actuator
  set, actuator alignment, press-mode query, port close. The state machine never reaches data reading.
- **What the per-slot poll does with state 5:** it calls `func_001279B8`, which only resets the slot's
  buttons and counters. So the original, in this environment, clears the buttons every frame forever.

## The decomp disagrees with the binary here

The decomp's `func_001275A0` maps state 5 to `func_00127BD8` (error count), 6 to `func_001279B8` (reset) and 7
to `func_001279C0` (data processing). The generated code of the same function (the binary is the final
authority) maps them as follows, which is also what the public libpad numbering suggests:

| state | meaning (libpad) | function called |
|---|---|---|
| 0, 3, 4 | disconnected / other | `func_001279A0` (full reset) |
| 1 | pad found | `func_001279A8` (reset buttons) |
| 2 | controller type check | `func_001279B0` (process, flag 0) |
| 5 | executing a command | `func_001279B8` (reset buttons) |
| 6 | stable | `func_001279C0` (process, flag 1: reads the report) |
| 7 | error | `func_00127BD8` (count the error) |
| 0x63 | closed | `func_001279A0`, then the poll reports done |

Consequences: (1) the native port follows this table, not the decomp's; (2) the decomp's three branches are
swapped and must be reported to the decomp owner; (3) a device that reports 6 (stable) after a port is
opened makes the data path run, which settles the open question of the plan.

## Decisions for the native device

- Opened and connected port: state 6. Disconnected or never opened: 0. Closed: 0x63 is not produced.
- Commands (main-mode set, actuator set) complete immediately; request state is always 0 (complete).
- The read returns the number of bytes of the report for the current mode (4, 8 or 20); the module rejects
  anything outside 1..31, so 32 is never returned.
- Unknown: the meaning of the Sony call made right after init (`func_0010DEC0`, argument 0). The device returns
  1 and logs it; revisit when the module's behaviour shows what it needs.

## Native `pad_device` execution (Task 5)

Handler contract taken from `samuelcaldas/age-of-empires-2-decomp`, `src/kcet/kt_libpad.c` and
`include/kt_libpad.h` (clean working tree at the time). The 14 Sony pad entry points were identified by their
call sites in `kt_libpad.c` and the arguments passed: init and start (`func_00125C58`), slot maximum
(`func_00126040`), port open (`func_00127488`), port close (`func_00127750`), state (`func_001275A0`), read
(`func_00127E68`), request state, actuator info (`func_00127820`), mode info (`func_00127900`), main mode,
direct actuator set (`func_00128390`), actuator alignment, and the pressure-mode query.

**Run 1** (headless, real gamepad backend, no gamepad attached; 90 s): the device reported state 0 for the
port in every one of 550 polls, because the host reports no controller. The generated module therefore kept
resetting, exactly as it must for a disconnected pad. This says nothing about the stable-state hypothesis; it
shows the device is faithful when nothing is connected.

**Run 2** (same, with the diagnostics-only synthetic backend, `AOE2_PAD_SYNTHETIC=1`: port 0 connected,
Cross toggling every 500 ms): the hypothesis holds.

| observation | original (no native pad) | native `pad_device` |
|---|---|---|
| state returned | 5 forever | 6 for port 0, 0 for port 1 (no pad) |
| per-state function | `func_001279B8` (reset), 256+ | `func_001279C0` (process, flag 1), 256+ |
| read called | never | 256+, returns 8 (analog report) |
| report processing, buttons update | never | `func_00127E68`, `func_00127C88`: 256+ |
| actuator update | never | `func_00128390`: 256+ |
| init, slot max, open | returned 1 (inferred) | 1, 1 for both ports, 1 for both ports |

The mode-info call answers 0x41 for table entry 0 (digital) and the module then selects the analog mode; the
pressure-mode query returns 1. Rumble was not exercised: the game did not request any actuator output in 90 s
(no `[synthetic-pad] rumble` line), so the vibration path is verified only by unit tests so far.

The validator still ends `stalled_in_loop` (the game's own stall is a separate problem; this module neither
fixes nor worsens it in this window).

## Where the decomp of `kt_libpad` disagrees with the binary

Found while preparing the native port by comparing the decomp with the generated code of the same functions
(the binary is the authority). Each item needs a fix in the decomp before that function is ported from it.

1. **`func_001275A0` (per-state poll).** The decomp maps state 5 to `func_00127BD8`, 6 to `func_001279B8` and
   7 to `func_001279C0`. The binary maps 5 to `func_001279B8`, 6 to `func_001279C0` and 7 to `func_00127BD8`
   (table above).
2. **`func_001273D8` (per-slot step).** The decomp always advances the step and resets the state. The binary
   advances the step (wrapping at 3) and resets the state only when the step function returns non-zero; the
   flags bit `0x40000` is cleared before the call either way. The step table in the ELF is
   `[func_00127488 (open port), func_001275A0 (poll state), func_00127750 (close port)]`, so a pad that is open
   and polling stays on step 1.
3. **`func_00127820` (actuator descriptors).** The call that reads one actuator descriptor byte is
   `(port, slot, actuator, term)` with four arguments; the decomp shows a fifth, spurious `-1`.
4. **`func_001285F8` / `func_00127900` (mode info).** The decomp lost an argument: the count call passes the
   mode-table selector (4) and leaves the offset register as the caller left it; the library meaning is
   offset `-1` for the count. The mode-id loop leaves the offset unset (ignored for selectors 1 to 3).
5. **Not in the decomp at all:** `func_00125A70` (slot address by index), `func_00125AA8` and `func_00125AC8`
   (the semaphore lock and unlock) and `func_00125AE8`; they sit inside the module range but have no
   definition in `kt_libpad.c`.

Consequence for the plan: the native device (Tasks 3 to 5) is unaffected and already works. Tasks 6 to 8 (the
port of the module itself) cannot rely on the decomp for control flow in the branch-heavy functions
(`func_00127488`, `func_001276B8`, `func_00127750`, `func_001279C8`, `func_00127E68`, `func_00128390`,
`func_001281C0`, `func_001286A8`, `func_00128878`, `func_00126C30`) until each is checked against the binary.

## Second pass: verification of every function (2026-10-24)

A two-verifier plus judge pass over all 105 functions of the module (decomp at main commit `3ca89e917`)
produced a noisy result: 25 functions marked as diverging, 14 undetermined. Many verifiers could not locate or
follow the generated code (the whole first group came back undetermined), judges disagreed with verifiers, and
several claims were wrong when checked by hand. The raw output is not tracked (it is not trustworthy as a
contract). What was **confirmed by hand against the generated code**:

Real divergences (the decomp must be fixed; the binary behaviour is):

- **`func_00126BC0` (rapid-fire setup).** The binary writes at `slot + 0x38 + ch*4`: a 16-bit mask at offset 0,
  zero at offset 2, and the byte `((mode + 0xF) & 0xF0) | (speed & 0xF)` at offset 3. The decomp writes into
  the mode-info area at `+0x1C` with the raw mode and speed bytes.
- **`func_00127488` (port request).** The buffer handed to the library's port open is `slot + 0x100` (256
  bytes, 64-byte aligned, owned by the library), and the alignment check is on that address. The decomp uses
  `dma_buffer` at `+0xC0`. Consequently the decomp header is also wrong: the pad report area is the 0x40 bytes
  at `+0xC0` (report bytes start at `+0xC4`), not a 0x140-byte `dma_buffer`.
- **`func_00127BE8` (report read).** After the read into `slot + 0xC4` with result `r`: when `r` is in 1..31
  the bytes `report[r..31]` are filled with `0xFF`; in every case `r` is stored as a 32-bit word at `slot + 0xC0`
  and `r` itself is returned (also when it is 0 or 32 and above). The decomp returns 0 for an out-of-range
  result, returns the result otherwise, and neither fills nor stores.
- **Comparisons against 3.** `func_00125F68` and `func_00126140` compare `n_pad` and `n_ports` as unsigned.
  Harmless for valid input.

Claims that were **wrong** (the decomp is right there): `func_00127750` (the `xori` is followed by a
conditional move that yields "result equals 1"), `func_00127900` (returns 1 when the word changed, 0 when equal),
`func_00127C88` (stores exactly what the decomp says), `func_00127820` (the per-actuator term runs 1 to 4, the
decomp's `j + 1`), and the zeroing offsets claimed for `func_00125DB8`.

Not checked (the judges' claims on `func_001279C8`, `func_00127E68`, `func_00128130`, `func_001281C0`,
`func_00128390`, `func_001286A8`, `func_00128878`, `func_00128990`, `func_00128A90`, `func_00128B68`,
`func_00127D88`, `func_00127DC8`, `func_001287E8`, `func_00128820`): they involve large jump tables or helper
calls and need a careful read of each function; do not trust the claims, and do not port from the decomp for these
until someone has.

At this second pass, whole-module fidelity remained unverified. The later scoped corrections and runtime
comparison below supersede the implementation hold; an oracle over identical memory and inputs is still
needed for arbitrary-input equivalence.


## Native-pad cleanup validation (2026-10-24)

Removed the fabricated two FINDPAD polls and eight FINDCTP1 polls, along with the unused command-busy
counters. The device again reports 6 immediately for an opened, connected port and 0 for a disconnected
or closed port; configuration commands complete synchronously and request state is always 0. Tests use
the backend's actual connection flag, including disconnect and reconnect, without forcing production states.

Repeated `PortOpen` previously cleared the rumble tracking while motors could still be active. The new
regression test fails against the pre-cleanup device with `repeated open stops both active motors`.
`PortOpen` now stops tracked active rumble before resetting tracking; the device test passes, including
closing after the repeated open without a redundant stop.

Validation: `cmake --build build` passed (20 build steps). `ctest --test-dir build --output-on-failure`
ran 14 tests: 13 passed and `kt_libpad_model` failed with `FAIL: slot is ready after the mode request`
(exit 8). The test exits on its first failure, so later model cases are not covered by that run. The report
read case was also run independently: the new exact assertion that the neutral left-Y report byte is 128
passes, along with the report length and tail checks. `git diff --check` passed.

**Unresolved model blocker:** an independent frame trace with the normal connected backend shows phases
0 → 1 → 2 → 3 → 5 → 0, then repeating 1 → 3 → 5 → 0; state remains 6. In `ConfigureAlignment`, successful
`SetActAlign` becomes result -1 and leaves `kChanged` set. With immediate request completion the phase
advances, but the report-processing phase finds alignment still pending and calls `ResetButtons`, resetting
the phase to zero. The later cycles skip the alignment phase and never reach ready. Read-only comparison
with `/home/samuelcaldas/repos/aoe/src/kcet/kt_libpad.c` at commit
`a29c5618c69e4ef71927df8a12563585eb3e6a0f` confirms the same result/flag handling in `func_001281C0` and
phase transitions/reset in `func_001279C8`. This source therefore does not resolve the model's control-flow
problem; settling it requires the binary oracle described above. The model and readiness assertion remain
unchanged; no state substitutions, polling delays or synthetic connectivity were introduced to hide it.

## Scoped native parity corrections (2026-10-24, continuation)

This section supersedes the cleanup run's unresolved alignment blocker above. The six pre-existing
modified files were preserved. Read-only comparison used the generated originals and the owner's
`src/kcet/kt_libpad.c` at `a29c5618c69e4ef71927df8a12563585eb3e6a0f`; that decomp still
contains the search, packing and mode discrepancies described here. No decomp or generated source was edited.

Confirmed against the original generated control flow and covered by focused regressions:

- `FindAlignment` selects a two-byte type pair from the guest GP-relative table, then searches all six
  four-byte descriptors for both bytes. Type zero returns the unused marker. A match in descriptor six
  failed before correction and passes after it.
- `ConfigureAlignment` takes type IDs from triples beginning at slot offset `0x54`, searches the descriptor
  area at `0x1C`, and clears each triple's value byte. Descriptor length zero consumes one byte;
  other lengths are retained. Output commands are packed at the accumulated byte offset, leaving padding
  as `0xFF`. An overflowing descriptor retains its match index and old offset, and consumes no output space;
  unmatched entries receive `0xFF`. Tests cover offsets, zero and two-byte lengths, overflow, and the exact
  six-byte command at the model/device boundary. A link-time observer avoids adding device policy or test
  hooks to production. Restoring the old command-index write in a temporary copy makes that regression fail.
- `ApplyMode` preserves an explicit requested ID. With parameter zero it suppresses a request only when
  the requested ID equals the current ID; with a nonzero parameter and zero ID it uses the current ID.
  Failed commands below retry six return -1; ordinary requests increment the counter, parameter three
  leaves it unchanged. At the limit the counter resets and ordinary requests stop retrying; parameter
  three remains pending. The explicit-ID and ordinary retry regression failed before correction and passes
  after correction. The host fixture now requests analog ID `0x73` explicitly; its earlier `0x41, 0x32`
  arguments selected analog only because of the erroneous XOR.

`RdramPadMemory` now validates whole scalar and bulk accesses through upstream `resolveEeGuestRange`;
invalid reads return zero and invalid writes leave memory untouched. Tests cover RAM and scratchpad ends,
valid aliases, absent backing memory, and wrapping bulk addresses. Snapshot hashing validates the full
512-byte slot and rejects subtraction underflow. Both boundary regressions failed before correction and
pass after it. These checks do not change the runtime's general guest address translation policy.

**Limits:** entry coverage is not behavioral verification. The coverage test compares actual decomp function
bodies (excluding prototypes/calls) with the union of replaced and explicitly deferred entries, plus the
four documented binary-only helpers. Only that comparison skips when the owner's decomp is absent;
name uniqueness, address uniqueness and instruction alignment checks always run. The async/thread/RPC and
allocator entries remain deferred. Other unverified functions listed in the second-pass section remain
unverified; these focused cases do not establish whole-module equivalence, arbitrary-input equivalence,
or in-game readiness. The existing successful-alignment and phase-machine edits were retained, and the
host readiness assertions now pass without invented polling delays. No game run or full game build was made during that earlier scoped pass.

Final validation: `cmake --build build` succeeded; `ctest --test-dir build --output-on-failure`
passed all 14 host tests. `python3 -m unittest discover -s tests` passed 301 tests (two existing skips).
Both `kcet_libpad.cpp` and `kcet_pad.cpp` passed direct `g++ -std=c++20 -msse4.1 -fsyntax-only`
checks with the runtime/IOP/Kernel and project include paths used by the focused adapter tests.
`git diff --check` passed. The coverage table contains 97 replaced and eight deferred entries.

## Sequential runtime comparison and observation limit (2026-10-24)

Always-run coverage assertions now require exactly **105 total entries, 97 replacements and eight
explicitly deferred entries**. The decomp-definition comparison remains skippable independently.
The existing adapter range checks and all prior model/device corrections were preserved. The alleged
shift problem was not changed: an eight-bit mode shifted right by four is in 0..15.

Independent baseline and post-change checks: all 14 existing host CTest cases passed (exit 0);
`python3 -m unittest discover -s tests` passed 301 tests, two skips (exit 0). Focused memory,
snapshot, coverage and alignment checks passed five tests (exit 0). The required
`python3 tools/docker_build.py --platform desktop --diagnostics --jobs 4` completed successfully
(exit 0), compiling both adapters, the backend and call trace and linking the game.

That build initially observed return from the outer `Update`. The original run demonstrated that the
game reaches `StepSlot` through a path that bypasses the `Update` replacement/trace. No snapshot was
emitted. The Sony read-handler snapshot was removed because native `ReadReport` bypasses it: it never
covered both implementations. The final shared diagnostic helper now observes return from `StepSlot`,
with the entry argument saved before the call, per-slot sample numbers, full 512-byte slot dumps/hashes,
and per-port synthetic backend sample counters. The trace suppresses its observer when the native
adapter owns it, preventing double counting. Cross still toggles every 100 port-zero samples; neither
state delays nor hardware stubs were added. The focused test checks both slots at identical sample
numbers and counters and rejects invalid whole-slot ranges.

**Validation boundary:** the successful full build and the following runtime logs predate the final
`StepSlot` observation correction. That correction passes the Python compile-and-run tests, but still
needs the required Docker rebuild and fresh sequential 90-second runs. There are no observed matching
snapshot sample numbers or slot bytes to compare; no behavioral parity or full equivalence is claimed.
The module remains off by default. The original trace file and the original absence of
`native_modules.ini` are restored after the sequential experiment by `finally` cleanup.

Both runs used `AOE2_PAD_SYNTHETIC=1 AOE2_PAD_SNAPSHOT=1`, 90 seconds, interval 30,
`pad_device=on`; original `pad=off`, native `pad=on`, sequentially. Each log was copied immediately.

- **original:** validator exit 1, `stalled_in_loop`, running at end, one real frame, no changed frames, zero missing branch targets; zero snapshots. Log: `/tmp/libpad-compare-original.log`.
- **native:** validator exit 1, `stalled_in_loop`, running at end, three real frames, no changed frames, zero missing branch targets; zero snapshots. Log: `/tmp/libpad-compare-native.log`.

Cleanup verified exact restoration of the 1,701-byte original trace and absence of the module file.
Final adapter and trace `g++ -std=c++20 -msse4.1 -fsyntax-only` checks passed (exit 0).

## Fresh first-snapshot actuator configuration correction (2026-10-24)

The fresh observer logs `/tmp/libpad-observer-original-fresh.log` and
`/tmp/libpad-observer-native-fresh.log` supersede the absence-of-snapshots limit above:
34 snapshots per run, no skips, and all 34 matched hashes differ. At `n=1, slot=0`,
both backend counters are zero. The complete 512-byte comparison has exactly these differences:

| slot offset | original | native before correction |
|---|---|---|
| `0x54` | 1 | 0 |
| `0x57` | 2 | 0 |
| `0xA4` | 0 | 1 |
| `0xA6` | 0 | 2 |

The cause is confirmed in the generated original `func_00126810` (`SetActuatorAlign`):
the table pointer comes from argument register `a1`; six consecutive source bytes go to
the type bytes beginning at `slot + 0x54`, with destination stride three. It then marks
the pad changed. Native code instead wrote to the derived descriptor/offset pairs at
`slot + 0xA4`, stride two. The minimal correction changes only that destination expression.
The config triples are `(type, value2, value1)` at `+0x54`; the derived alignment pairs
at `+0xA4` are separate. The header's existing explicit offsets already represent this
distinction; decomp field names must not be used to infer the setter's destination.

Other initialization paths were checked before making the correction:

- Generated `InitSlot` clears the entire 512-byte slot, sets mode/parameter zero,
  enable mask `0x90`, port/request fields to -1, invalidates state, and clears six
  value bytes backwards from `+0x63`, stride three. Native behavior agrees.
- No generated body for `func_00126750` was found in this checkout. Read-only decoding
  of the owned ELF confirms its actual argument uses: `a0` is pad, `a1` actuator,
  `a2` becomes the byte at `+0x56 + actuator*3`, and `a3` the byte at
  `+0x55 + actuator*3`. The native setter already agrees; no argument swap is justified.
- Generated `ClearActuator` clears only `+0x55 + actuator*3`, or all six such bytes
  for actuator -1. It preserves the type byte and second value byte, as native code does.

The new host regression reconstructs exactly the four observed original/native byte
triples, compares all 512 bytes after setting the initial type table before `StepSlot`,
and exercises all six types together with setter/clear preservation. It failed before
correction at the full-slot comparison. An existing output-only rumble fixture now
sets its derived descriptor indices explicitly instead of relying on the erroneous
setter writes; all its existing backend assertions remain. No generated code or ELF
bytes were copied into tracked files, and no game build or run was performed.

Validation: `cmake --build build -j 4` and all 14 host CTest cases pass after correction.
`python3 -m unittest discover -s tests` passes 301 tests (two skips). Direct
`g++ -std=c++20 -Ipad/include -fsyntax-only` checks of the touched model and C++
test pass, as does `git diff --check`. No Python source was changed.
Fresh runtime first-snapshot equality and later snapshot/state equivalence still require
orchestrator rebuild/reruns; the host reconstruction does not claim runtime parity.


### Second observer snapshot: mode query and phase control flow (2026-10-24)

The alignment-fixed comparison supplied by the orchestrator has 34 snapshots per
run, 18 equal hashes, and an equal first snapshot. The first remaining mismatch
is snapshot 2, slot 0, at equal sample counts (2/0): offset `0x37` is 115/65,
`0x88` is 3/1, and `0xBC` is 115/0 (original/native). These are confirmed model
translation errors, not evidence for adding device delays.

Read-only inspection of generated `FUN_00127900_0x127900.cpp` confirms that the
four `InfoMode` queries use terms 1 through 4, but do **not** all use offset zero.
The initial offset is zero; after each call its result is truncated to a byte,
stored in the packed word, and passed as the next offset. For the existing native
analog device the queries return current ID `0x73`, extended ID `0x79`, current
table index 1, then table entry 1 (`0x73`). The packed word is consequently
`0x73017973`, not `0x41017973`. `PadDevice.InfoMode` already supplies these values;
its mode table needs no change. The native model now preserves the argument chain.

Read-only inspection of generated `FUN_001279c8_0x1279c8.cpp` confirms:

- A **zero** report-read result skips model comparison. A nonzero result compares
  high nibbles and stores the complete report mode byte regardless of equality
  (the store occupies the equality branch's delay slot). Only a changed high
  nibble resets the local phase and sets the model-change event.
- The seven ELF jump-table entries select setup, mode, wait, alignment, wait,
  pressure, wait. Phase zero therefore runs setup; it does not skip initialization.
  After setup it increments the phase and executes the mode command in the **same
  call**, including when the unchanged-word branch skips descriptor refresh.
  A mode helper returning zero adds two more phases, reaching phase three; a
  successful command adds one, reaching phase two. The native early return after
  setup was incorrect.
- The default report phase receives the actual read result, including zero,
  rather than an unconditional success flag.

The focused regression reconstructs exactly the three differing byte values and
compares the corrected result across **all 512 slot bytes**. Additional checks
cover unchanged packed words, phase-zero command success/no-op, same-high-nibble
model updates, and failed reads retaining the model. The existing alignment test
fixture now supplies its already-known model before entering phase three, so a
newly detected controller does not legitimately reset that isolated scenario.

Validation: host CMake/Ninja build with game/probe disabled and all 14 CTest tests
pass; Python unittest discovery passes 301 tests (two skipped). Syntax-only C++
checks pass for the libpad/pad overrides, backend (with the existing raylib include
path), and call-trace unit; Python test-module compilation and `git diff --check`
pass. No game build/execution or fresh observer capture was performed. Full-run
snapshot equality and readiness remain for the orchestrator to verify.

### Pressure flag polarity and the extra connectivity sample (2026-10-24)

Read-only comparison of `/tmp/libpad-observer-original-fresh.log` with
`/tmp/libpad-observer-native-phase-fixed.log` confirms 34 snapshots each and 21
identical slot hashes. Snapshots 1–4 match slot bytes. At snapshot 5, slot 0,
only byte `+0xB2` differs: original 11, native 3 (`0x80000` missing).

The original generated ApplyPressMode establishes the polarity unambiguously:
it tests the Sony query result for equality to 1, forms both set-bit and
clear-bit flag values, and conditionally selects the clear-bit value when that
equality test is **zero**. Thus success sets `0x80000`; every other result clears
it. Read-only decoding of the corresponding owned ELF instructions confirms the
query-result XOR, unsigned equality test, and conditional-move instruction match
the generated translation. The inherited name `kNoPressure` is misleading; the
minimal fix preserves its identifier and corrects the selection, without masking
snapshot bytes or modifying the device's successful query response.

The generated helper control flow also proves that PressModeA returns 1 for
requests 1 or 2, otherwise 0; PressModeB returns 1 only for request 2, otherwise
0. Their native translations incorrectly returned 2 on several paths. Both
predicates are corrected. Original ApplyPressMode compares A with whether the
reported model is pressure ID `0x79`. Equality finishes locally. Otherwise A=0
selects Sony ExitPressMode with local result zero; A=1 selects EnterPressMode and
uses B as the local result. It invokes the selected callback with port and slot
only when InfoPressMode returned 1. Callback return 1 produces result 1; any
other return produces -1. Without support, the local result determines zero
versus -1. Nonnegative completion clears pending; a negative result retains it.
These findings come from generated original control flow, not decomp inference.

The extra sample predates the flag mismatch: snapshot 3, slot 0 already has
original counters 4/1 versus native 5/1; snapshot 5 has 8/3 versus 9/3. Alignment
success calls StopActuators in both paths. In the original generated wrapper,
the final tail call is a direct C++ call to the generated Sony SetActDirect body,
bypassing runtime replacement dispatch. The normal Sony handlers still replace
State, Read, SetActAlign and InfoPressMode, as confirmed by the original log.
The native stop path instead calls PadDevice::SetActDirect, whose Connected
check samples the host once even for zero or unchanged actuator values. State
and Read each sample once; SetActAlign and InfoPressMode do not sample. This
provides a concrete native sampling source consistent with the offset at alignment;
the snapshot counters alone do not prove which original dispatch path ran.
It is not evidence of an extra input frame or a pressure-query sample. Device
sampling policy and counters are unchanged. The existing synthetic observer
uses the outer slot's logical step, so extra queries cannot advance its input.

Regression coverage compares all 512 slot bytes for both initial bit states and
Sony query returns -1, 0, 1 and 2, verifies both helper truth tables (including
out-of-range requests), and counts device samples across state, read, alignment,
pressure query and repeated zero actuator commands. Existing logical-step tests
also remain in the Python suite.

Limitations: the native ApplyPressMode still lacks Sony enter/exit callback
invocation and its failure/retry semantics; correcting the helper predicates does
not implement those commands. The observed default request/model takes the
original equality/no-callback path, so this gap does not explain the supplied
snapshot mismatch. No callback implementation or generated tail-call dispatch
repair is included in this narrow correction. No game build or execution was
performed; fresh observer equality and physical pressure-controller behavior
remain unverified and require orchestrator follow-up.

Validation for this correction: `cmake -B build -G Ninja && cmake --build build`
passes with the existing game/probe-disabled cache; `ctest --test-dir build
--output-on-failure` passes all 14 tests. `python3 -m unittest discover -s tests`
passes 302 tests with two skips (its negative-path fixtures print expected error
messages). `git diff --check` passes. Existing dirty work was preserved.

### Sixth observer snapshot: report edges, analog and pressure data (2026-10-24)

Read-only comparison of `/tmp/libpad-observer-original-fresh.log` and
`/tmp/libpad-observer-native-pressure-fixed.log` finds 34 snapshots each and
**21 byte-identical pairs** (not the supplied summary's 22). Slot zero agrees
through n5. At n6, counters are 10/4 versus 11/4; the only byte differences are
`+0x02` (0/64), `+0x08..0x0B` (0/128), and `+0x10..0x1B` (0/255).
The existing counter offset is retained; no timing, input, hashes or field
exclusions were changed.

The generated original report processor and helpers establish these corrections:

- The analog gate tests flag bit 4 and an extended-mode predicate; native already
  agrees. Analog config bytes are `(mode, low, high, center)`, whereas native
  passed `(high, mode, low, center)`. Report stick bytes are consumed backwards
  and packed into raw and scaled words, as native already does.
- ScaleWithThreshold has **four** arguments. It caps high at one less than the
  smaller distance from center to an endpoint, converts the input into distance
  from center (subtracting an extra one on the lower side), scales to 128 levels,
  negates below center, and returns the low byte. The high-limit conditional move
  executes in a branch delay slot. There is no early zero-return condition based
  solely on which side of center the input occupies. The wrapper and declaration
  now preserve all four arguments. ScaleByMode's mode-two branch forwards low
  and high, not high and center; its argument moves include delay slots.
- The default pressure source is the constant table. A conditional move retains
  that default when the pressure-ID predicate is zero; only ID 0x79 selects the
  report payload. Native had the selection reversed.
- Recovery from the report-failed flag explicitly zeros the newly pressed mask
  before clearing the flag. That explains the first Cross edge difference.
- StoreButtons itself agrees with the original, including its return delay-slot
  store. Its arguments are held, newly pressed, and **rapid output**, not released.
  The caller initializes rapid output to zero and accumulates masked edges or
  repeat pulses. On a fresh masked edge its countdown receives the low nibble
  when the high nibble is zero, otherwise the high nibble (the latter store is in
  a branch delay slot). Held masks decrement nonzero countdowns and emit a pulse
  only at zero. Native now preserves this dataflow rather than computing a
  release edge. The existing release expectation was corrected accordingly.

Regression coverage reconstructs the observed n6 report, analog configuration
and recovery flag, compares all 512 slot bytes, and pins each differing byte's
corrected value. Additional cases cover normal pressed edges, pressure payload
selection, initial/held rapid output, signed scaling on both sides of center,
high-limit saturation, and mode-two parameter forwarding. The fixture uses a
zero default table; the supplied snapshots establish its observed zero payload.
No generated code, disassembly, or function-map content was copied into tracked
files.

Validation: `cmake --build build -j 4` passes with game/probe disabled in the
existing cache; `ctest --test-dir build --output-on-failure` passes all 14 tests.
`python3 -m unittest discover -s tests` passes 302 tests (two skips).
`git diff --check` passes. No game build or run was performed. All pre-existing
dirty files were retained.

Residual limits: these host regressions do not establish fresh runtime parity.
Zero-length, error-status and disabled-mode report exits have not been completed
as part of this observed valid-report correction; the generated processor has a
shared writeback path that the native early exits do not fully model. The
previously documented pressure callback gap also remains. Physical controller
behavior and later whole-run equality require orchestrator follow-up.


## Current bounded validation result (2026-10-24)

This result supersedes the earlier pending fresh-comparison statements above, without
establishing whole-module equivalence. There are **97 native replacements and eight
explicitly deferred entries** (105 total); `pad` remains off by default. The implementation
no longer awaits a corrected decomp. Other functions have not been exhaustively
differential-tested; pressure enter/exit callback and exceptional report-exit gaps
recorded above remain open.

Independent parsing of the supplied 30-second observer captures found:

- Original: `/tmp/libpad-observer-original-fresh.log`, SHA-256
  `f852fb03fd6ac4ac2ce77de3765da6213266ab098adb37f01da0493f448662ac`.
- Native: `/tmp/libpad-observer-native-report-fixed.log`, SHA-256
  `aa957463af00c7efcd2bfd0f2cc1d69af98dc9ffc79dcc3c49c31a7913937175`.
- Each log has 34 snapshots, zero snapshot skips and no duplicate keys. The key
  sets match exactly: logical steps 1–16 and 64, each for slots 0 and 1.
  All **34/34 complete 512-byte payloads** and their recorded FNV hashes match;
  equality was checked on decoded bytes, without exclusions.
- Sample-call counters are not equal. At `(n=3, slot=0)`, original is 4/1 and
  native 5/1; at `(n=64, slot=0)`, original is 126/62 and native 127/62.
  At slot 1 of step 64 they are 126/63 and 127/63. The port-0 delta is zero
  for steps 1–2 and +1 at every recorded key from step 3 onward; port 1 agrees.

Source inspection proves a native extra-sampling opportunity: successful alignment
calls `StopActuators`, which calls `PadDevice::SetActDirect`; its `Connected` check
samples even a zero or unchanged command. The existing executed device regression
counts this sample, while alignment and pressure queries themselves do not sample.
This is consistent with the first counter divergence, but no new per-call runtime
trace was captured here to prove exclusive attribution or the original dispatch path.

With the snapshot observer enabled, both outer-step adapters call `BeginPadStep`;
`SyntheticCrossDown` uses port 0's logical step minus one, toggling every 100
steps, independently of sample-call counts. Without logical steps it falls back
to sample count. The captures only record through step 64: this narrow scenario
uses a connected port 0 with Cross held and neutral sticks, and disconnected port 1.
It does not demonstrate a Cross release/repeat cycle or physical pressure/rumble.
Sparse snapshots also do not establish equality at every intervening call.

The supplied run assessment remains `stalled_in_loop`, with no changed frames;
this is a validator limitation/result, not evidence of playability. The observer
logs alone do not contain the validator verdict or establish the elapsed duration;
the 30-second window and verdict are supplied capture context. No game build or
run was performed for this validation. Real rumble/hardware remain unverified.

Current host validation: `cmake --build build` exited 0 (`ninja: no work to do`),
with game/probe disabled in the existing cache; `ctest --test-dir build
--output-on-failure` passed all 14 tests. `python3 -m unittest discover -s tests`
ran 302 tests: 300 passed and two skipped (exit 0). Its negative-path fixture
errors are expected. The four new standalone C++ regression sources are compiled
and executed by `test_native_kt_libpad_alignment.py`,
`test_native_kt_libpad_pressure.py`, and `test_native_pad_memory.py` (two sources).
The expanded model/device C++ suites run through CTest. No test-wiring edit was
needed. `git diff --check` passed.
