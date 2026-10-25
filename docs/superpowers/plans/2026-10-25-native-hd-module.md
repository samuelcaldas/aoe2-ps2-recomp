# Native HD Module Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the native C++ replacement for KCET's hard drive / virtual disc subsystem (`kt_hd`) over guest memory (`GuestAccess`) and host file access, replacing low-level SIF/IOP disk calls behind the configurable switch `hd=on`, and verify byte-level parity.

**Architecture:** A pure model (`KtHdModel`) manages table lookups (`HdMapEntry` 8-byte tuples `{key, val}`), status/mode flag queries, callback dispatch, and work area registers (`D_4EA180`, `D_4ED300`). A storage helper (`HdStorageModel`) provides host file access for "host0:", "sim:", and "atfile:" prefixes. Game overrides (`kcet_hd_module.cpp`) bind guest registers atomically, set `pc = ra`, and write return values to `v0`.

**Tech Stack:** C++20, CMake, Ninja, Python 3 unittest, deterministic snapshot observation (`[state-snapshot]`), `GuestAccess`/`RdramGuestAccess`.

**Spec:** Item 6 of `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` and decomp spec `docs/recomp/kt_hd.yaml`.

## Global Constraints

- Never commit game binaries, ISOs, Ghidra projects, function maps, or generated C++ (`CLAUDE.md`).
- Recompiled binary (`generated/SLES-50282/`, read-only) is the absolute authority over the decomp.
- The decomp (`samuelcaldas/age-of-empires-2-decomp`, local `~/repos/aoe`) is authorized for direct code reference (`CLAUDE.md`).
- Non-blocking execution contract: guest handlers run atomically without blocking guest threads.
- Switch in `game/SLES-50282/native_modules.ini` (`hd=on`) gates installation; default remains `off`.

## Review Focus

1. **Table lookup boundary:** `func_00129588` (`ktHdTableLookup`) handles count <= 0, missing keys, and exact matches in 8-byte `{key, val}` pairs.
2. **Flag state queries:** `func_00128F40` sets and checks binary flag (0 or 1), returning current flag; `func_00128F68` queries without modification.
3. **Hardware register read-and-clear:** `func_00129548` reads and clears register offset (reading `D_4ED300[0x16]`), ensuring subsequent reads return 0.
4. **Callback invocation safety:** `func_00129560` validates non-null function pointer before execution and returns 0 on NULL.
5. **Snapshot observer determinism:** Frame-level snapshots record consistent state without duplicating records when native module is active.

---

### Task 1: Function Coverage Map and Entry Inventory

**Files:**
- Create: `hd/data/hd_module_entries.txt`, `tests/test_native_hd_entries.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `hd/data/hd_module_entries.txt` defining the verified guest addresses, names, and call signatures (`ENTRY_R`, `ENTRY_V`, `DEFERRED`) for `kt_hd` (27 functions).

- [x] **Step 1: Write the failing test**

```python
# tests/test_native_hd_entries.py
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class TestNativeHdEntries(unittest.TestCase):
    def test_entries_file_exists_and_has_valid_format(self):
        entries_path = ROOT / "hd" / "data" / "hd_module_entries.txt"
        self.assertTrue(entries_path.exists(), "hd_module_entries.txt must exist")
        lines = [line.strip() for line in entries_path.read_text().splitlines() if line.strip() and not line.startswith("#")]
        self.assertGreaterEqual(len(lines), 20, "Must cover at least 20 hd functions")
        for line in lines:
            parts = line.split()
            self.assertIn(parts[0], {"ENTRY_R", "ENTRY_V", "DEFERRED"})
            self.assertTrue(parts[1].startswith("0x"), f"Invalid address format: {parts[1]}")
```

- [x] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests.test_native_hd_entries`
Expected: FAIL with missing file.

- [x] **Step 3: Create `hd/data/hd_module_entries.txt`**

Map the verified entry points from `docs/recomp/kt_hd.yaml` (27 functions) against `analysis/generated/function_map.csv`.

- [x] **Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests.test_native_hd_entries`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add hd/data/hd_module_entries.txt tests/test_native_hd_entries.py
git commit -m "test: function coverage map for native hd module"
```

---

### Task 2: State Snapshot Observer for HD Subsystem

**Files:**
- Create: `game_hooks/src/hd_snapshot.hpp`, `tests/cpp/hd_snapshot_tests.cpp`
- Modify: `game_hooks/src/aoe2_call_trace.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `ObserveHdState(uint8_t* rdram, uint64_t step)` that emits `[state-snapshot] label=hd` capturing HD work area (`D_4EA180`, `D_4ED300`).

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/hd_snapshot_tests.cpp
#include "hd_snapshot.hpp"
// test formatting of kHdRegion
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_hd_snapshot_tests`
Expected: FAIL with compilation error (hd_snapshot.hpp not found).

- [x] **Step 3: Implement snapshot observer in `game_hooks/src/hd_snapshot.hpp` and wire into `aoe2_call_trace.cpp`**

Emit FNV-1a hash and byte hex representations of `kHdRegion` at logical frame intervals.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R hd_snapshot --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add game_hooks/src/hd_snapshot.hpp game_hooks/src/aoe2_call_trace.cpp tests/cpp/hd_snapshot_tests.cpp
git commit -m "feat: hd state snapshot observer"
```

---

### Task 3: Pure HD State Model (`KtHdModel`)

**Files:**
- Create: `hd/include/aoe2/hd/kt_hd_model.hpp`, `hd/src/kt_hd_model.cpp`, `tests/cpp/kt_hd_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `aoe2::runtime::GuestAccess`
- Produces: `class KtHdModel`
  - `int32_t TableLookup(uint32_t table_addr, int32_t count, int32_t key)`
  - `int32_t SetFlag(int32_t flag)`
  - `int32_t GetFlag() const`
  - `int32_t ReadAndClearRegister()`
  - `void SetHostname(std::string_view name)`
  - `int32_t ExecuteCallback(uint32_t func_ptr, uint32_t arg_ptr)`

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/kt_hd_model_tests.cpp
// test TableLookup, flag setters/getters, register read-and-clear
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_kt_hd_model_tests`
Expected: FAIL with missing header.

- [x] **Step 3: Implement `KtHdModel` in `hd/src/kt_hd_model.cpp`**

Implement table lookup, flag management, register read-and-clear following `src/kcet/kt_hd.c` and decomp specs.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R kt_hd_model --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add hd/include/aoe2/hd/kt_hd_model.hpp hd/src/kt_hd_model.cpp tests/cpp/kt_hd_model_tests.cpp
git commit -m "feat: native kt_hd model over guest memory"
```

---

### Task 4: Host File/HD Storage Model (`HdStorageModel`)

**Files:**
- Create: `hd/include/aoe2/hd/hd_storage_model.hpp`, `hd/src/hd_storage_model.cpp`, `tests/cpp/hd_storage_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `aoe2::runtime::GuestAccess`
- Produces: `class HdStorageModel`
  - `bool Initialize(std::string_view root_path)`
  - `int32_t ReadFile(std::string_view filename, uint32_t dest_guest_addr, int32_t size, int32_t offset)`
  - `int32_t FileExists(std::string_view filename)`

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/hd_storage_model_tests.cpp
// test host file read and offset resolution
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_hd_storage_model_tests`
Expected: FAIL with missing header.

- [x] **Step 3: Implement `HdStorageModel` in `hd/src/hd_storage_model.cpp`**

Implement host file reading and path resolution for virtual disc files without SIF IOP dependencies.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R hd_storage_model --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add hd/include/aoe2/hd/hd_storage_model.hpp hd/src/hd_storage_model.cpp tests/cpp/hd_storage_model_tests.cpp
git commit -m "feat: native hd storage model for host disc files"
```

---

### Task 5: Native Module Adapters, Entries, and Switches

**Files:**
- Create: `game_hooks/src/kt_hd_entries.inc`, `game_hooks/src/kcet_hd_module.cpp`, `tests/cpp/native_hd_adapter_tests.cpp`
- Modify: `cmake/Aoe2Recompiled.cmake`

**Interfaces:**
- Produces: `InstallNativeHd(PS2Runtime&)` activated by `hd=on` in `native_modules.ini`.
- Uses `PS2_REGISTER_GAME_OVERRIDE` to replace guest functions. Handlers set `ctx->pc = getRegU32(ctx, 31)` and `setReturnU32(ctx, result)`.

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/native_hd_adapter_tests.cpp
// Test argument mapping and return value conventions for HD functions
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_native_hd_adapter_tests`
Expected: FAIL.

- [x] **Step 3: Implement adapter in `kcet_hd_module.cpp`**

Implement X-macro table in `kt_hd_entries.inc`. Bind handlers in `kcet_hd_module.cpp`.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R native_hd_adapters --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add game_hooks/src/kt_hd_entries.inc game_hooks/src/kcet_hd_module.cpp tests/cpp/native_hd_adapter_tests.cpp
git commit -m "feat: native hd module adapter behind switch"
```

---

### Task 6: Differential Testing of Pure HD Routines

**Files:**
- Create: `tests/cpp/hd_differential_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Reference implementations and `KtHdModel` methods.
- Produces: Differential tests verifying table lookups, flag getters/setters, and register read-and-clear.

- [x] **Step 1: Write failing differential tests**

```cpp
// tests/cpp/hd_differential_tests.cpp
// Compare reference functions against native model on identical vectors
```

- [x] **Step 2: Run test to verify execution**

Run: `cmake --build build --target aoe2_hd_differential_tests`
Expected: Verify compilation and execution.

- [x] **Step 3: Implement test vectors and edge cases**

Cover table lookup keys (first, mid, last, missing, count <= 0), binary flag transitions, and register read-and-clear sequence.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R hd_differential --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add tests/cpp/hd_differential_tests.cpp
git commit -m "test: differential verification of pure hd routines"
```

---

### Task 7: Original vs Native Snapshot Comparison to Observed Parity

**Files:**
- Modify: `docs/sot/hd-semantics.md` (new SOT), `docs/sot/kcet-stall-log.md`, `docs/superpowers/plans/2026-10-25-native-hd-module.md`

- [x] **Step 1: Rebuild game and run 30s original baseline capture**

Run: `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1 python3 tools/validate_desktop.py --run-seconds 30 --interval 5` (`hd=off`)
Save log to `run-hd-orig.log`.

- [x] **Step 2: Enable `hd=on` in `native_modules.ini` and run 30s native capture**

Run: `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1 python3 tools/validate_desktop.py --run-seconds 30 --interval 5` (`hd=on`)
Save log to `run-hd-native.log`.

- [x] **Step 3: Compare snapshots byte for byte**

Compare all snapshot lines across both runs. Confirm matching FNV checksums and byte buffers. 38/38 snapshots matched byte-for-byte.

- [x] **Step 4: Record observed parity, bounds, and limits**

Update documentation with snapshot counts, exact checksum matches, and remaining gaps. Recorded in `docs/sot/hd-semantics.md` and `docs/sot/kcet-stall-log.md`.

- [x] **Step 5: Commit**

```bash
git add docs/sot/ docs/superpowers/plans/2026-10-25-native-hd-module.md
git commit -m "docs: record native hd observed parity"
```

---

## Self-Review

1. **Spec Coverage:** Implements Item 6 of `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md` and fulfills `docs/recomp/kt_hd.yaml`.
2. **Step Scan:** Every task follows strict TDD (failing test first, implementation, verification, commit).
3. **Type Consistency:** Standardized on `GuestAccess`, `RdramGuestAccess`, and the `HdMapEntry` layout defined in `include/kt_hd.h`.
4. **Review Focus:** Explicitly tests table lookup bounds, flag state queries, hardware register read-and-clear, and deterministic snapshot capture.
5. **Proportion:** Clear, bite-sized tasks structured identically to the completed pad, mouse, keyboard, and memory card plans.
