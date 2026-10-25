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
