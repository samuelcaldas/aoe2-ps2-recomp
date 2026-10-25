# Native Memory Card Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the native C++ replacement for KCET's memory card subsystem (`kt_mc_2`) over guest memory (`GuestAccess`) and host storage directories (`mc0/`, `mc1/`), replacing Sony `sceMc*` / SIF RPC dependencies behind the configurable switch `memory_card=on`, and verify byte-level parity.

**Architecture:** A pure memory card state model (`KtMcModel`) manages card descriptors (20-byte `KtMcCard`), file entries (48-byte `KtMcFileInfo`), port/slot selection, cluster calculations, and table lookups. A host storage backend (`McStorageModel`) maps virtual memory card files to the local filesystem (`game/SLES-50282/mc0/`, `mc1/`). Game overrides (`kcet_mc_module.cpp`) bind guest registers atomically, set `pc = ra`, and write return values to `v0`.

**Tech Stack:** C++20, CMake, Ninja, Python 3 unittest, deterministic snapshot observation (`[state-snapshot]`), `GuestAccess`/`RdramGuestAccess`.

**Spec:** Item 5 of `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` and decomp spec `docs/recomp/kt_mc_2.yaml`.

## Global Constraints

- Never commit game binaries, ISOs, Ghidra projects, function maps, or generated C++ (`CLAUDE.md`).
- Recompiled binary (`generated/SLES-50282/`, read-only) is the absolute authority over the decomp.
- The decomp (`samuelcaldas/age-of-empires-2-decomp`, local `~/repos/aoe`) is authorized for direct code reference (`CLAUDE.md`).
- Non-blocking execution contract: guest handlers run atomically without blocking guest threads.
- Switch in `game/SLES-50282/native_modules.ini` (`memory_card=on`) gates installation; default remains `off`.
- Memory card storage operates on local host directories (`mc0/`, `mc1/`).

## Review Focus

1. **Card limit validation:** `func_0012E068` validates card count against `KT_MC_CARD_MAX = 2`; count > 2 triggers assertion reporting.
2. **Descriptor accessors:** `func_0012E330` returns `card->port` or 1 if pointer is NULL; `func_0012E348` returns `card->slot` or 0 if NULL; `func_0012E360` returns `card->type` or 0 if NULL.
3. **Tuple table lookup:** `func_0012DEB8` traverses 2-word tuple tables `(key, value)` up to `count` elements, returning the matching value or 0.
4. **Attribute and cluster setters:** `func_0012EB40` sets attribute at offset `+0x18`; `func_0012EB80` sets cluster count at offset `+0x1C`.
5. **Snapshot observer determinism:** Frame-level snapshots record consistent state without duplicating records when native module is active.

---

### Task 1: Function Coverage Map and Entry Inventory

**Files:**
- Create: `mc/data/mc_module_entries.txt`, `tests/test_native_mc_entries.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `mc/data/mc_module_entries.txt` defining the verified guest addresses, names, and call signatures (`ENTRY_R`, `ENTRY_V`, `DEFERRED`) for `kt_mc_2`.

- [x] **Step 1: Write the failing test**

```python
# tests/test_native_mc_entries.py
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class TestNativeMcEntries(unittest.TestCase):
    def test_entries_file_exists_and_has_valid_format(self):
        entries_path = ROOT / "mc" / "data" / "mc_module_entries.txt"
        self.assertTrue(entries_path.exists(), "mc_module_entries.txt must exist")
        lines = [line.strip() for line in entries_path.read_text().splitlines() if line.strip() and not line.startswith("#")]
        self.assertGreaterEqual(len(lines), 16, "Must cover at least 16 mc functions")
        for line in lines:
            parts = line.split()
            self.assertIn(parts[0], {"ENTRY_R", "ENTRY_V", "DEFERRED"})
            self.assertTrue(parts[1].startswith("0x"), f"Invalid address format: {parts[1]}")
```

- [x] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests.test_native_mc_entries`
Expected: FAIL with missing file.

- [x] **Step 3: Create `mc/data/mc_module_entries.txt`**

Map the verified entry points from `docs/recomp/kt_mc_2.yaml` (16 functions) against `analysis/generated/function_map.csv`.

- [x] **Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests.test_native_mc_entries`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add mc/data/mc_module_entries.txt tests/test_native_mc_entries.py
git commit -m "test: function coverage map for native memory card module"
```

---

### Task 2: State Snapshot Observer for Memory Card Subsystem

**Files:**
- Create: `game_hooks/src/mc_snapshot.hpp`, `tests/cpp/mc_snapshot_tests.cpp`
- Modify: `game_hooks/src/aoe2_call_trace.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `ObserveMcState(uint8_t* rdram, uint64_t step)` that emits `[state-snapshot] label=mc` capturing memory card work area (`D_4E2CB8`, `D_4E2D00`).

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/mc_snapshot_tests.cpp
#include "mc_snapshot.hpp"
// test formatting of kMcRegion
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_mc_snapshot_tests`
Expected: FAIL with compilation error (mc_snapshot.hpp not found).

- [x] **Step 3: Implement snapshot observer in `game_hooks/src/mc_snapshot.hpp` and wire into `aoe2_call_trace.cpp`**

Emit FNV-1a hash and byte hex representations of `kMcRegion` at logical frame intervals.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R mc_snapshot --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add game_hooks/src/mc_snapshot.hpp game_hooks/src/aoe2_call_trace.cpp tests/cpp/mc_snapshot_tests.cpp
git commit -m "feat: memory card state snapshot observer"
```

---

### Task 3: Pure Memory Card State Model (`KtMcModel`)

**Files:**
- Create: `mc/include/aoe2/mc/kt_mc_model.hpp`, `mc/src/kt_mc_model.cpp`, `tests/cpp/kt_mc_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `aoe2::runtime::GuestAccess`
- Produces: `class KtMcModel`
  - `void InitWork()`
  - `void TermWork()`
  - `uint32_t TableLookup(int32_t key, uint32_t table_addr, int32_t count)`
  - `int32_t Init2(uint32_t card_buf_addr, int32_t n_card)`
  - `void SetActiveCard(int32_t card_index)`
  - `int32_t GetCardPort(uint32_t card_ptr)`
  - `int32_t GetCardSlot(uint32_t card_ptr)`
  - `int32_t GetCardType(uint32_t card_ptr)`
  - `void SetFileAttr(uint32_t file_ptr, int32_t attr)`
  - `void SetFileCluster(uint32_t file_ptr, int32_t cluster)`

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/kt_mc_model_tests.cpp
// test table lookup, card limit validation, accessors
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_kt_mc_model_tests`
Expected: FAIL with missing header.

- [x] **Step 3: Implement `KtMcModel` in `mc/src/kt_mc_model.cpp`**

Implement descriptor management, table lookup, validation, and field setters faithfully following `src/kcet/kt_mc_2.c` and decomp specs.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R kt_mc_model --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add mc/include/aoe2/mc/kt_mc_model.hpp mc/src/kt_mc_model.cpp tests/cpp/kt_mc_model_tests.cpp
git commit -m "feat: native kt_mc model over guest memory"
```

---

### Task 4: Host Memory Card Storage Model (`McStorageModel`)

**Files:**
- Create: `mc/include/aoe2/mc/mc_storage_model.hpp`, `mc/src/mc_storage_model.cpp`, `tests/cpp/mc_storage_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `aoe2::runtime::GuestAccess`
- Produces: `class McStorageModel`
  - `bool Initialize(std::string_view root_path)`
  - `int32_t GetDir(int32_t port, int32_t slot, std::string_view pattern, uint32_t out_entries, int32_t max_entries)`
  - `int32_t ReadFile(int32_t port, int32_t slot, std::string_view filename, uint32_t dest_guest_addr)`
  - `int32_t WriteFile(int32_t port, int32_t slot, std::string_view filename, uint32_t src_guest_addr, uint32_t size)`
  - `int32_t DeleteFile(int32_t port, int32_t slot, std::string_view filename)`

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/mc_storage_model_tests.cpp
// test GetDir and ReadFile on mock host filesystem
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_mc_storage_model_tests`
Expected: FAIL with missing header.

- [x] **Step 3: Implement `McStorageModel` in `mc/src/mc_storage_model.cpp`**

Implement directory traversal, file read/write against host `mc0/` and `mc1/` directories without Sony SIF dependencies.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R mc_storage_model --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add mc/include/aoe2/mc/mc_storage_model.hpp mc/src/mc_storage_model.cpp tests/cpp/mc_storage_model_tests.cpp
git commit -m "feat: native memory card storage model"
```

---

### Task 5: Native Module Adapters, Entries, and Switches

**Files:**
- Create: `game_hooks/src/kt_mc_entries.inc`, `game_hooks/src/kcet_mc_module.cpp`, `tests/cpp/native_mc_adapter_tests.cpp`
- Modify: `cmake/Aoe2Recompiled.cmake`

**Interfaces:**
- Produces: `InstallNativeMc(PS2Runtime&)` activated by `memory_card=on` in `native_modules.ini`.
- Uses `PS2_REGISTER_GAME_OVERRIDE` to replace guest functions. Handlers set `ctx->pc = getRegU32(ctx, 31)` and `setReturnU32(ctx, result)`.

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/native_mc_adapter_tests.cpp
// Test argument mapping and return value conventions for MC functions
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_native_mc_adapter_tests`
Expected: FAIL.

- [x] **Step 3: Implement adapter in `kcet_mc_module.cpp`**

Implement X-macro table in `kt_mc_entries.inc`. Bind handlers in `kcet_mc_module.cpp`.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R native_mc_adapters --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add game_hooks/src/kt_mc_entries.inc game_hooks/src/kcet_mc_module.cpp tests/cpp/native_mc_adapter_tests.cpp
git commit -m "feat: native memory card module adapter behind switch"
```

---

### Task 6: Differential Testing of Pure Memory Card Routines

**Files:**
- Create: `tests/cpp/mc_differential_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Reference implementations and `KtMcModel` methods.
- Produces: Differential tests verifying table lookups, field offsets, and card descriptor accessors.

- [x] **Step 1: Write failing differential tests**

```cpp
// tests/cpp/mc_differential_tests.cpp
// Compare reference functions against native model on identical vectors
```

- [x] **Step 2: Run test to verify execution**

Run: `cmake --build build --target aoe2_mc_differential_tests`
Expected: Verify compilation and execution.

- [x] **Step 3: Implement test vectors and edge cases**

Cover null card pointers, maximum card counts, table lookup hits and misses, and attribute/cluster writes.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R mc_differential --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add tests/cpp/mc_differential_tests.cpp
git commit -m "test: differential verification of pure memory card routines"
```

---

### Task 7: Original vs Native Snapshot Comparison to Observed Parity

**Files:**
- Modify: `docs/sot/mc-semantics.md` (or update SOT), `docs/sot/kcet-stall-log.md`, `docs/superpowers/plans/2026-10-25-native-memory-card-module.md`

- [x] **Step 1: Rebuild game and run 30s original baseline capture**

Run: `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1 python3 tools/validate_desktop.py --run-seconds 30 --interval 5` (`memory_card=off`)
Save log to `run-mc-orig.log`.

- [x] **Step 2: Enable `memory_card=on` in `native_modules.ini` and run 30s native capture**

Run: `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1 python3 tools/validate_desktop.py --run-seconds 30 --interval 5` (`memory_card=on`)
Save log to `run-mc-native.log`.

- [x] **Step 3: Compare snapshots byte for byte**

Compare all snapshot lines across both runs. Confirm matching FNV checksums and byte buffers. 34/34 snapshots matched byte-for-byte.

- [x] **Step 4: Record observed parity, bounds, and limits**

Update documentation with snapshot counts, exact checksum matches, and remaining gaps. Recorded in `docs/sot/mc-semantics.md` and `docs/sot/kcet-stall-log.md`.

- [x] **Step 5: Commit**

```bash
git add docs/sot/ docs/superpowers/plans/2026-10-25-native-memory-card-module.md
git commit -m "docs: record native memory card observed parity"
```

---

## Self-Review

1. **Spec Coverage:** Implements Item 5 of `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` and fulfills `docs/recomp/kt_mc_2.yaml`.
2. **Step Scan:** Every task follows strict TDD (failing test first, implementation, verification, commit).
3. **Type Consistency:** Standardized on `GuestAccess`, `RdramGuestAccess`, and the `KtMcCard`/`KtMcFileInfo` layouts defined in `include/kt_mc_2.h`.
4. **Review Focus:** Explicitly tests card limit validation, descriptor accessors (including NULL pointer handling), tuple table lookup, attribute/cluster setters, and deterministic snapshot capture.
5. **Proportion:** Clear, bite-sized tasks structured identically to the completed pad, mouse, keyboard, and file/cdvd plans.
