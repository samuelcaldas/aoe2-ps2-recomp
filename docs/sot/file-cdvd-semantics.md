# File and CDVD Subsystems Semantics and Observed Parity

This document records the baseline determinism, function coverage, binary contracts, and observed parity for KCET's file table subsystem (`kt_file`) and CD/DVD server (`kt_libcdvd_server`).

## Architecture and Contracts

- **Memory Models:**
  - `KtFileModel` (`file/include/aoe2/file/kt_file_model.hpp`) operates purely over `aoe2::runtime::GuestAccess`.
  - Global `CdServerState` is located at guest `0x004F1508` (20 bytes: `record_table`, `record_count`, `min_val`, `entry_table`, `entry_count`).
  - Global `CdStreamStatus` is located at guest `0x004F16C0` (12 bytes: `field0`, `active`, `bytes_remaining`).
  - Binary search (`func_0012D128` / `BinarySearch`) operates over 8-byte `FileRecord` structures (`u16 id, u16 aux, u32 payload`), comparing keys against `id & 0x7FFF`.
  - Kind classification (`func_0012CEA0` / `ClassifyKind`) classifies handles into kinds 0..3 (0: flag bits set, 1: exact tag 0x40, 2: other non-zero tags, 3: null pointer).

- **Disc Reading and Streaming Model:**
  - `CdvdModel` (`cdvd/include/aoe2/cdvd/cdvd_model.hpp`) provides sector reading and streaming simulation over host disc files (`game/SLES-50282/aoe2.iso` or extracted data).
  - CD-ROM sector size is 2048 bytes; `bytes_remaining >> 11` represents remaining sectors.
  - Queued reads update `CdStreamStatus` at `0x004F16C0` atomically.

- **Switches and Game Overrides:**
  - Gated behind switches `file=on` and `cdvd=on` (or `file_cd=on`) in `game/SLES-50282/native_modules.ini`. Default is `off`.
  - Overrides register via `PS2_REGISTER_GAME_OVERRIDE` in `kcet_file_module.cpp` and `kcet_cdvd_module.cpp`.

## Observed Parity (2026-10-25)

A 30-second differential capture under `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1` compared the original recompiled baseline (`file=off, cdvd=off`) against the native C++ module implementations (`file=on, cdvd=on`).

Results:
- Both runs produced 34 state snapshots across steps 1..16 and step 64.
- 34/34 snapshots matched byte-for-byte with identical FNV-1a checksums and state buffers (0 mismatches).
- Validation assessment remained `stalled_in_loop` (`real_frames=2, frames_changed=False`), confirming that neither file nor cdvd module causes the boot state machine stall.
- All 25 host CTests passed cleanly.
