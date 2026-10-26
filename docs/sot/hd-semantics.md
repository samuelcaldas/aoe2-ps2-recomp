# Hard Drive Subsystem Semantics and Observed Parity

This document records the baseline determinism, function coverage, binary contracts, and observed parity for KCET's hard drive / virtual disc subsystem (`kt_hd`).

## Architecture and Contracts

- **Memory Models:**
  - `KtHdModel` (`hd/include/aoe2/hd/kt_hd_model.hpp`) operates purely over `aoe2::runtime::GuestAccess`.
  - Global `HdMapEntry` tuples (8 bytes: `{key, val}`).
  - Global work area at `0x004EA180` (holds active mode/status flags).
  - Register read-and-clear (`func_00129548`) reads `D_4ED300[0x16]` (at `0x004ED300 + 0x16 * 4`) and zeroes the field.
  - Table lookup (`func_00129588` / `TableLookup`) scans 2-word tuple tables and returns mapped values or 0 on failure.
  - Flag setter/getter (`func_00128F40`, `func_00128F68`) manages mode flags with bounds validation.

- **Host File/HD Storage Model:**
  - `HdStorageModel` (`hd/include/aoe2/hd/hd_storage_model.hpp`) resolves virtual disk paths with "host0:", "sim:", or "atfile:" prefixes to local host files.
  - Supports `ReadFile` with arbitrary offsets and `FileExists` queries without SIF IOP calls.

- **Switches and Game Overrides:**
  - Gated behind switch `hd=on` in `game/SLES-50282/native_modules.ini`. Default is `off`.
  - Overrides register via `PS2_REGISTER_GAME_OVERRIDE` in `kcet_hd_module.cpp`.

## Observed Parity (2026-10-25)

A 30-second differential capture under `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1` compared the original recompiled baseline (`hd=off`) against the native C++ module implementation (`hd=on`).

Results:
- Both runs produced 38 state snapshots (including 4 HD snapshots at steps 1, 2, 3, 4 with `fnv=8421ae126c7ced25`).
- 38/38 snapshots matched byte-for-byte with identical FNV-1a checksums and state buffers (0 mismatches).
- The four HD observations have an unchanged hash; this establishes neither exercised HD state transitions/storage parity nor full module equivalence. The other 34 observations concern mouse/keyboard state.
- Validation remained `stalled_in_loop`; these observations do not identify or exclude the cause of the stall.
- All 35 host CTests passed cleanly.
