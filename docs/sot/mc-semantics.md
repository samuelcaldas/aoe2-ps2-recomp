# Memory Card Subsystem Semantics and Observed Parity

This document records the current scaffolding and validation limits for KCET's memory card subsystem (`kt_mc_2`). No native MC replacement is currently supported. The model layouts and operations below describe existing code, not verified binary contracts: the boundary audit rejected the previously selected entry points, including internal continuation labels. The storage backend is not integrated into a verified game adapter.

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
  - Gated behind switch `memory_card=on` in `game/SLES-50282/native_modules.ini`. Default is `off`.
  - **Unsupported / unverified:** `kt_mc_entries.inc` currently contains no verified hook entry points. Enabling memory card throws an actionable error before allocating module state or querying/replacing runtime functions. Keep `memory_card=off` until verified hooks are available. Disabled installation remains a no-op.
  - Overrides register via `PS2_REGISTER_GAME_OVERRIDE` in `kcet_mc_module.cpp`.

## Observed Parity (2026-10-25)

A historical 30-second differential capture under `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1` compared runs configured with `memory_card=off` and `memory_card=on`. That capture predates the boundary audit which removed all MC entries; it does not validate the current empty table. Enabling the empty table previously reported `installed 0/0`; this unsupported configuration is now rejected explicitly.

The recorded 34 matching snapshots are mouse/keyboard observations. They establish
only input-state agreement during those runs; they do not establish storage or
module parity. No nonempty subsystem-specific packet/state comparison is recorded
here. The unchanged `stalled_in_loop` result does not identify or exclude the cause
of the boot stall. Historical host test counts describe unit coverage only.

## Empty-installation regression (2026-10-26)

`python3 -m unittest tests.test_native_mc_installer` compiles the actual installer
source with the actual entry map and minimal runtime/model boundary doubles. It
checks that `off` does not throw and that `on` throws with a recovery instruction,
without constructing module state, querying hooks, or reporting installation
success. Removing the guard reproduces the failure; restoring it passes. This
regression validates configuration rejection, not MC operations or storage parity.
A separate local check compiled the adapter against the actual upstream runtime
and confirmed the exception propagates through the real override registry; its
log is retained in ignored `build/hd-validation/mc-real-runtime.log`.
