# Memory Card Subsystem Semantics and Observed Parity

This document records the baseline determinism, function coverage, binary contracts, and observed parity for KCET's memory card subsystem (`kt_mc_2`).

## Architecture and Contracts

- **Memory Models:**
  - `KtMcModel` (`mc/include/aoe2/mc/kt_mc_model.hpp`) operates purely over `aoe2::runtime::GuestAccess`.
  - Global `KtMcCard` descriptors (20 bytes each: `port`, `slot`, `type`, `free_clusters`, `flags`).
  - Global `KtMcFileInfo` (48 bytes: `name[32]`, `size`, `attr`, `date[8]`).
  - Tuple table lookup (`func_0012DEB8` / `TableLookup`) searches 2-word tuple tables `(key, value)` up to `count` elements.
  - Card limit validation (`func_0012E068` / `Init2`) validates `n_card <= KT_MC_CARD_MAX` (2); larger values invoke assertion.
  - Descriptor accessors (`func_0012E330`, `0x12E348`, `0x12E360`) return descriptor fields or defaults (1 for port, 0 for slot/type) when given NULL.
  - File entry field setters write attribute at `+0x18` and cluster count at `+0x1C`.

- **Host Storage Model:**
  - `McStorageModel` (`mc/include/aoe2/mc/mc_storage_model.hpp`) handles filesystem operations across host directories `game/SLES-50282/mc0/` and `mc1/` without IOP/SIF RPC calls.
  - Directory listing (`GetDir`), file reads (`ReadFile`), writes (`WriteFile`), and deletions (`DeleteFile`).

- **Switches and Game Overrides:**
  - Gated behind switch `memory_card=on` in `game/SLES-50282/native_modules.ini`. Default is `off`.
  - Overrides register via `PS2_REGISTER_GAME_OVERRIDE` in `kcet_mc_module.cpp`.

## Observed Parity (2026-10-25)

A 30-second differential capture under `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1` compared the original recompiled baseline (`memory_card=off`) against the native C++ module implementation (`memory_card=on`).

The recorded 34 matching snapshots are mouse/keyboard observations. They establish
only input-state agreement during those runs; they do not establish storage or
module parity. No nonempty subsystem-specific packet/state comparison is recorded
here. The unchanged `stalled_in_loop` result does not identify or exclude the cause
of the boot stall. Historical host test counts describe unit coverage only.
