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

## Focused runtime repair proof (2026-10-26)

Semantic reference: the owner's `samuelcaldas/age-of-empires-2-decomp`,
`src/kcet/kt_hd.c`, commit `81a9dda41511ea5af676566cf23e14dca390cbac`.
The owned generated originals were inspected directly (the three
`FUN_*_0x129560.cpp`, `FUN_*_0x129428.cpp`, and `FUN_*_0x1294c0.cpp` units).
They establish these runtime contracts:

- `0x129560` reserves a 16-byte guest stack frame, saves the caller's 64-bit
  return address, copies the callback pointer into `v0`, and forwards `a1` in
  `a0` even for a null callback. A nonnull callback dispatches through the runtime
  and returns through `0x12957c`; its result is preserved. Null returns zero.
- `0x129428` reserves 64 bytes, saving `s0`, `s1`, and `ra`. It loads a signed
  semaphore ID from `gp - 0x7bd8` and copies the two-word guest dispatch table at
  `0x4e2c28` onto the guest stack. ID `-1` returns one without dispatch. Otherwise,
  zero selects the first guest target and any nonzero selector selects the second.
  The target receives the ID in `a0`. After continuation `0x129480`, the routine
  returns one only when the target's 64-bit result equals the saved signed ID.
- `0x1294c0` saves `ra` in a 16-byte guest frame and calls `0x129428` with selector
  one, returning through `0x1294d0`.

`game_hooks/src/hd_runtime_adapter.hpp` implements these three entries and their
three continuations. Suspended state lives in guest registers and stack frames.
Every guest call uses `dispatchGuestBranch`, retaining its scheduler cycle charge
and early-yield behavior. The constant-one replacements and model-only callback
API were removed. The table lookup and read-and-clear model entries remain.

The opt-in `hd_runtime_differential` CTest links the actual three generated units
and the cached upstream runtime, following the GPU differential harness pattern.
Before the repair, 144 comparisons reported mismatches for all three routines
(RED). After the repair, the expanded 156-comparison suite passed (GREEN):

- Null/non-null callbacks; zero, one, and all-bits-set selectors; absent/present
  IDs; matching/mismatching callback results; synchronous/yielding guest targets.
- Distinct dispatch-table targets, nested callback frames, both halves of all
  32 GPRs, return PC/branch PC, and the affected 80-byte guest stack region.
- Execution under the real EE scheduler, comparing final cycle counts and stack
  observations at deadlines of one and eight cycles.
- Real upstream semaphore poll/wait operations with initial counts zero and one.
  The empty wait asserts that one waiter is blocked, then signals at a scheduled
  deadline and verifies completion, final semaphore counts/waiters, and
  original/native state parity. The test
  supplies guest target adapters for these operations; it does not establish the
  production dispatch table's initialization or the identity of its targets.

Reproduce from the repository root (paths may be overridden for another owned
checkout and compatible cached build):

```sh
cmake -S . -B build-hd -G Ninja -DAOE2_BUILD_HD_RUNTIME_TESTS=ON \
  -DAOE2_HD_ORIGINAL_ROOT=/home/samuelcaldas/source/repos/aoe2-ps2-recomp \
  -DAOE2_HD_CACHED_BUILD=/home/samuelcaldas/source/repos/aoe2-ps2-recomp/build/docker-desktop-diagnostics
cmake --build build-hd -j 2
ctest --test-dir build-hd --output-on-failure
```

This verifies the exercised entry and continuation paths on desktop x86-64 with
that cached runtime. It does not prove arbitrary interior-PC entry, all scheduler
interleavings, production semaphore initialization, storage/IOP behavior, or game
progress. The earlier unchanged snapshot capture remains a separate, weaker
observation.
