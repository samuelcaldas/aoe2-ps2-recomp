# Mouse and keyboard modules: observed areas, synthetic input and baseline

Evidence for the native `kt_mouse` / `kt_keybd` work (plan `docs/superpowers/plans/2026-10-24-native-mouse-keyboard-modules.md`).
The recompiled binary is the authority; the decomp is read only as a map. No generated code is quoted here.

## Work areas used by the snapshot observer

Taken from the `extern` work-area symbols of the decomp sources and bounded by the neighbouring modules:

| Module | Symbols (first to last) | Region logged | Why this bound |
|---|---|---|---|
| mouse | `D_509F00` .. `D_50A280` (an 8-word RPC argument block, so the last byte is `0x50A2A0`) | `0x509F00` + `0x3C0` (to `0x50A2C0`) | every symbol in the range belongs to `kt_mouse.c`; the next symbol is the keyboard's first work area |
| keyboard | `D_50A2C0` .. `D_50A4C0` | `0x50A2C0` + `0x280` (to `0x50A540`) | every symbol belongs to `kt_keybd.c`; `0x50A540` is the first symbol of the USB keyboard driver data (RPC client status words, not module state) |

The regions are contiguous BSS ranges, so alignment gaps between symbols are included (they stay zero). The USB driver data
is not snapshotted: its RPC functions are already replaced by the host device layer.

Per-frame update functions (the capture points): mouse `func_00135BF0` (ktMouseProcessEvents), keyboard `func_00137000`
(ktKeybdProcessEvents). Both are the `ProcessEvents` entries named by the decomp headers, and both run hundreds of times in
the boot trace (see below). The original path is observed through the call trace wrapper (the two addresses must be in
`trace_addrs.txt`); the native handlers will call the same `OnModuleUpdate`.

## Synthetic input

`AOE2_INPUT_SYNTHETIC=1` replaces the host mouse and keyboard with a pure function of the LOGICAL STEP (completed per-frame
updates of the module), not of RPC reads or time, so original and native see the same input at the same step:

- mouse: motion `(+3, -2)` every 50 steps (reported once per step), left button down for steps 200..399, 600..799, ...
- keyboard: key A (HID usage 0x04) held during the first 30 steps of every 120.

`AOE2_STATE_SNAPSHOT=1` logs `[state-snapshot] label n steps fnv bytes` for n = 1..16 and every 64th afterwards, plus
`[state-snapshot-config]` and explicit `[state-snapshot-skip]` lines.

## Boot trace (90 s, desktop diagnostics build, game stalled in a loop)

The game calls 22 of 45 functions of `kt_mouse`, 24 of 42 of `kt_keybd` and 4 of 6 of `kt_usbkeybd_drv`. The 6 functions of
`kt_usbkeybd_drv` and 4 of `kt_mouse` (bind, get info, read, device info) are already native through the host device
layer. The decomp owner's audit confirmed the six USB keyboard functions against the binary.

## Baseline determinism (2026-10-24)

Two independent 30 s runs of the ORIGINAL code (diagnostics build rebuilt after the observer commit; pad on; synthetic pad, mouse
and keyboard; `AOE2_STATE_SNAPSHOT=1`; call trace limited to the two update functions), both ending `stalled_in_loop`:

| Region | Snapshots run A / run B | Common keys | Byte-identical | Skips |
|---|---|---|---|---|
| mouse (0x3C0 bytes) | 17 / 17 | 17 | 17 | 0 |
| keyboard (0x280 bytes) | 17 / 17 | 17 | 17 | 0 |

The comparison instrument is therefore noise-free for this scenario: any later difference between original and native is a
real behavioural difference. Snapshots cover logical steps 1..16 and 64.

How much the oracle sees: the mouse area has 17 non-zero bytes at step 1 and 24 at step 64 (seven bytes differ between the
first and last snapshot, two distinct hashes); the keyboard area has 21 non-zero bytes throughout (three bytes differ, three
distinct hashes). The window reaches the first synthetic mouse motion (step 50) but not the first button toggle (step 200) nor the
second keyboard press (step 120). A longer window (60 s or more) would widen coverage and should be used before declaring parity.

## Function coverage map (2026-10-25)

`game_hooks/src/input_module_entries.txt` classifies every function of both modules exactly once, checked by
`tests/test_input_modules_coverage.py` (and against the decomp sources when the checkout is present).

| Module | Functions | ENTRY (planned replacement set) | DEVICE (already native) | DEFERRED |
|---|---|---|---|---|
| kt_mouse | 45 | 32 | 4 (RPC bind/get_info/read/device_info) | 9 |
| kt_keybd | 42 | 35 | 0 | 7 |
| kt_usbkeybd_drv | 6 | the six `kcet.keyboard.*` roles | | |

Deferral evidence:
- The four lock/unlock functions (mouse 0x134F90/0x134FB8, keyboard 0x136008/0x136030) are called only from inside
  their own module. Native ports run atomically inside one handler and skip module semaphores, so they are not replaced.
- Semaphore creation and the wait loop (e.g. 0x134F20, 0x135D60) reach kernel services and stay with the kernel adapter.
- 0x134DE8 removes an interrupt handler; it is a kernel-module service also called by generated kernel and disc-server code.

## Native mouse model (Task 4, 2026-10-25)

`mouse/` implements the 32 ENTRY functions as a host-only guest-memory model: initialization,
ring access, allocation, payloads, record reconciliation and event processing. The two alignment
entries are genuine empty returns, verified directly in the owned ELF; they are not device stubs.
`MouseRpc` supplies the existing device boundary without introducing a guest dispatch or wait.
The origin commit is recorded in each ported source/header.

Binary checks preserve the following non-obvious behavior:
- Initialization accepts zero and two mice (and does not reject negative counts); only counts above
  two trigger the original assertion. The plan's wording suggesting assertions for zero/two was wrong.
- Slots are 0xB0 bytes; four 0x24-byte entries start at +0x20. ResetQueue receives slot +0x14,
  clears +0x0C for 0x90 bytes, then resets its +8 and +4 indices. An occupied write entry drops
  the new event but PostEvent still returns success for an allocated port.
- A negative payload length is clamped to eight through the original unsigned comparison.
  The reader does not impose an additional length cap. Negative mutation indices return -1;
  upper bounds invoke the original assertion instead of silently clamping.
- Device-info count and event bytes are signed; buttons, dx, dy and wheel are sign-extended
  to the queued words. A report type below four suppresses the wheel, even when negative.
- Port-state access is signed 16-bit. A reset of inactive ports changes only that state;
  it does not clear payloads or queues. Forced resets count every slot, including disconnected ones.

The model tests cover initialization boundaries, 0/1/4/5 queued events, wrapping and reuse,
allocation exhaustion, full/disconnected queues, payload clamping, conditional release, signed
reports and draining until empty. These tests are not original/native snapshot evidence.
Runtime registration and shared RPC adapters remain Task 6; no mouse switch is enabled by
this change and no new game run or parity claim is made here.

## Native keyboard model (Task 5, 2026-10-25)

`keyboard/` implements the 35 keyboard ENTRY functions over `GuestAccess`, with an abstract
`KeyboardRpc` for the six-function device layer. Slots are 0x78 bytes; four 0x14-byte queue
entries begin at +0x28. Each entry contains a ready word and a 16-byte packet (two header
bytes plus up to 13 payload bytes, zero-filled through the 14-byte payload area). Lengths
of 14 or more trigger the original assertion. Full queues discard new packets but return
success for an allocated port. Init accepts zero or one keyboard; two triggers the assertion.
The two alignment entries were verified as empty returns directly in the owned ELF.

Binary review did not confirm the earlier suspected discrepancy at `func_00136850`:
negative mutation indices return -1, allocated ports can change state, and upper bounds
invoke the original assertion. No decomp control-flow correction was required for this task.

Request naming must not be used to infer device command identity. The decomp's Repeat
request (+0x14) calls the device SetLed RPC; its Led request (+0x18) calls SetCapsType.
The native model preserves that mapping. Both mark pending requests with bit 31, send
only the low byte, and acknowledge only on a zero device return. `KeyboardRpc` command
methods take both the device index and value, like the original USB wrappers. No new
repeat timer is introduced: repeated packets, presses and releases are preserved in order.

The signed packet-length assertion does not reject a negative length; the original memcpy
helper interprets its count as unsigned 32-bit. The port preserves this conversion rather
than silently treating negative sizes as zero. The normal device path supplies an unsigned
byte length. Invalid-memory behavior for an enormous copy is not claimed equivalent.

Host tests cover allocation exhaustion, empty/full/wrapped queues, packet header truncation,
13/14-byte boundaries and zero padding, payload clamping, release/reset, request retry and
acknowledgment, RPC errors, and press/repeat/release drained within the same update.
Model and processing tests are separate translation units of the same CTest target.
A review claim that negative-size padding differed was rejected: both original and native
compute `entry + 6 + size` (entry +5 for size -1), with wrapping 32-bit address arithmetic.

Runtime adapters remain Task 6; no keyboard switch was enabled and no game build/run or
original/native snapshot comparison was performed in this task. Unit tests and static
binary review do not establish runtime parity.

## Native module adapters and switches (Task 6, 2026-10-25)

`game_hooks/src/kcet_mouse_module.cpp` and `kcet_keyboard_module.cpp` wire the native models
to the runtime via `PS2_REGISTER_GAME_OVERRIDE`, gated behind `native_modules.ini` switches
(`mouse=on` and `keyboard=on`).

Design and execution contract:
- The device RPC services (`MouseRpc`, `KeyboardRpc`) are exposed as shared thread-safe singletons
  in `native_input_services.cpp` via `EnsureMouseRpc()` and `EnsureKeyboardRpc()`, so device handlers
  and native module handlers invoke the same state without coupling initialization order.
- Each module adapter verifies that the device layer is configured and present in `kcet_hooks.ini`.
  If a module switch is enabled but device hooks are missing, module installation fails fast
  with a descriptive error.
- Function replacements require `hasFunction(address)`. The four binary alignment padding
  functions (mouse `0x134F18`, `0x135D98`; keyboard `0x135F90`, `0x137200`) are valid empty returns
  in the owned binary that do not have generated function units; the validator explicitly permits them.
- Per-frame snapshots (`OnModuleUpdate`) occur exactly once per frame at the return of `ProcessEvents`.
  When a native module is installed, `aoe2_call_trace.cpp` suppresses call-trace snapshot capture
  for that module to avoid duplicate records.
- Argument binding translates guest registers `a0..a3` and `t0..t3` into typed values, sets `pc = ra`,
  and assigns `v0` for value-returning functions. Model execution is atomic within each handler,
  honoring the non-blocking recompilation contract.

## Native mouse and keyboard observed parity (Task 7, 2026-10-25)

A 30-second differential comparison between the original recompiled game code
(`mouse=off, keyboard=off`) and the native module implementations (`mouse=on, keyboard=on`)
was executed on the desktop validation runner under deterministic synthetic input and
state snapshot capture (`AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1`).

Measurement results:
- **Snapshot count:** Both runs produced exactly 34 state snapshots (17 keyboard, 17 mouse)
  at logical steps 1 through 16 and step 64, with zero dropped or duplicate records.
- **Byte parity:** 34/34 snapshots matched byte-for-byte across all fields, with identical
  FNV-1a 64-bit checksums and identical memory dumps:
  - Step 1: keyboard `fnv=4a55d94f060a8109`, mouse `fnv=4a64f49b7f28aa53`
  - Steps 2–16: keyboard `fnv=fce31a51e47fb209`, mouse `fnv=4a64f49b7f28aa53`
  - Step 64: keyboard `fnv=c0e5ba9190a348b9`, mouse `fnv=1e1c37cab910a075`
- **Validation assessment:** `stalled_in_loop` (`real_frames=4, frames_changed=False`) in both
  runs, confirming that neither the mouse nor keyboard module is responsible for the boot
  state machine stall.
- **Coverage bounds and limits:**
  - The 30-second synthetic input profile exercises port allocation, device connection status,
    packet queue initialization, cursor delta updates, button clicks, and single-key press/release
    cycles during the boot sequence.
  - Full HID 6-key rollover, mid-execution physical disconnect/reconnect cycles, and host-device
    LED hardware interaction were verified through isolated unit tests (`kt_mouse_model_tests`,
    `kt_keybd_model_tests`, and `native_input_adapter_tests`), not differential game capture.


