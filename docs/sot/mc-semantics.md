# Memory Card Subsystem Semantics and Observed Parity

This document records the current scaffolding and validation limits for KCET's memory card subsystem (`kt_mc_2`). The local game configuration uses `memory_card=off` to leave libmc calls on the guest/PS2Recomp path instead of installing the project's native host-backed libmc overrides. The PS2Recomp runtime provides filesystem-backed memory card services; end-to-end game save and load have not been verified. Existing card data must be preserved when changing backends.

## Unverified Model and Storage Scaffolding

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
  - `memory_card=off` in `game/SLES-50282/native_modules.ini` disables the native libmc installer; missing switches also default to off. The guest then uses the PS2Recomp IOP memory card services.
  - `memory_card=on` enables the experimental host-backed module, which attempts to replace 17 libmc entry points. Its installation tests do not establish in-game save/load correctness.
  - Overrides register via `PS2_REGISTER_GAME_OVERRIDE` in `kcet_mc_module.cpp`; changing the configuration requires restarting the game.

## Observed Parity (2026-10-25)

A historical 30-second differential capture under `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1` compared runs configured with `memory_card=off` and `memory_card=on`. That capture predates the current libmc implementation and does not validate it.

The recorded 34 matching snapshots are mouse/keyboard observations. They establish
only input-state agreement during those runs; they do not establish storage or
module parity. No nonempty subsystem-specific packet/state comparison is recorded
here. The unchanged `stalled_in_loop` result does not identify or exclude the cause
of the boot stall. Historical host test counts describe unit coverage only.

## Installation coverage

`python3 -m unittest tests.test_native_mc_installer` compiles the installer with
runtime boundary doubles and checks that disabling it leaves functions untouched
and enabling it installs the expected entries. This does not verify save/load
behavior or parity with PS2Recomp.
