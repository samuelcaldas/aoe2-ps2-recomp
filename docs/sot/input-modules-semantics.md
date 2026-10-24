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
