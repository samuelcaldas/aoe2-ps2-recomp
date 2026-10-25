# Native CDVD and File Modules Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the native C++ replacement for KCET's file subsystem and CD/DVD server (`kt_file` and `kt_libcdvd_server`) over guest memory (`GuestAccess`) and extracted disc files, replacing IOP/SIF dependencies behind configurable switches (`file=on`, `cdvd=on`), and verify byte-level parity.

**Architecture:** A pure host-side file table model (`KtFileModel`) handles memory-only record tables (8-byte `FileRecord`, binary search, kind dispatching, string matching). A disk I/O and streaming model (`CdvdModel`) fulfills sector reading and stream status queries directly from host files (extracted disc root in `game/SLES-50282/`). Game hooks (`kcet_file_module.cpp`, `kcet_cdvd_module.cpp`) bind guest registers atomically within non-blocking handlers, set `pc = ra`, and write return values to `v0`.

**Tech Stack:** C++20, CMake, Ninja, Python 3 unittest, deterministic snapshot observation (`[state-snapshot]`), `GuestAccess`/`RdramGuestAccess`.

**Spec:** `docs/superpowers/specs/2026-10-23-file-module-native-shim-design.md` and `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md`.

## Global Constraints

- Never commit game binaries, ISOs, Ghidra projects, function maps, or generated C++ (`CLAUDE.md`).
- Recompiled binary (`generated/SLES-50282/`, read-only) is the absolute authority over the decomp.
- The decomp (`samuelcaldas/age-of-empires-2-decomp`, local `~/repos/aoe`) is authorized for direct code and layout reference (`CLAUDE.md`).
- Non-blocking execution contract: guest handlers run atomically without blocking guest threads; deferred functions continue to run via generated code.
- Switches in `game/SLES-50282/native_modules.ini` (`file=on`, `cdvd=on`) gate installation; default remains `off`.
- Builds run with memory-capped systemd scope and C locale (`tools/docker_build.py` or host Ninja).

## Review Focus

1. **Record table binary search boundary:** `func_0012D128` handles 0 records, 1 record, missing IDs, and exact match at first/last elements without out-of-bounds reads.
2. **File kind classification:** `func_0012CEA0` maps pointers/tags into `KtFileKind` (0, 1, 2, 3) with exact handling of null pointers and invalid categories.
3. **Stream status state fields:** `CdStreamStatus` (`D_4F16C0`) correctly reports `field0`, `active`, and `bytes_remaining` (with `bytes_remaining >> 11` representing remaining sectors) during reading and idle states.
4. **Disc file resolution:** Sector-based and relative path queries resolve safely to host paths within `game/SLES-50282/` without path traversal or buffer overruns.
5. **Snapshot observer determinism:** Frame-level snapshots at module updates record consistent FNV hashes and byte buffers without duplicate entries when native modules are installed.

---

### Task 1: Function Coverage Map and Entry Inventory

**Files:**
- Create: `file/data/file_module_entries.txt`, `tests/test_native_file_entries.py`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Produces: `file/data/file_module_entries.txt` defining the verified guest addresses, names, and call signatures (`ENTRY_R`, `ENTRY_V`, `DEFERRED`) for `kt_file` (50 functions) and `kt_libcdvd_server` (17 functions).

- [x] **Step 1: Write the failing test**

```python
# tests/test_native_file_entries.py
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

class TestNativeFileEntries(unittest.TestCase):
    def test_entries_file_exists_and_has_valid_format(self):
        entries_path = ROOT / "file" / "data" / "file_module_entries.txt"
        self.assertTrue(entries_path.exists(), "file_module_entries.txt must exist")
        lines = [line.strip() for line in entries_path.read_text().splitlines() if line.strip() and not line.startswith("#")]
        self.assertGreaterEqual(len(lines), 50, "Must cover at least 50 file/cdvd functions")
        for line in lines:
            parts = line.split()
            self.assertIn(parts[0], {"ENTRY_R", "ENTRY_V", "DEFERRED"})
            self.assertTrue(parts[1].startswith("0x"), f"Invalid address format: {parts[1]}")
```

- [x] **Step 2: Run test to verify it fails**

Run: `python3 -m unittest tests.test_native_file_entries`
Expected: FAIL with missing file.

- [x] **Step 3: Create `file/data/file_module_entries.txt`**

Map the verified entry points from `docs/recomp/kt_file.yaml` (50 functions) and `kt_libcdvd_server.yaml` (17 functions) against `analysis/generated/function_map.csv`.

- [x] **Step 4: Run test to verify it passes**

Run: `python3 -m unittest tests.test_native_file_entries`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add file/data/file_module_entries.txt tests/test_native_file_entries.py
git commit -m "test: function coverage map for native file and cdvd modules"
```

---

### Task 2: State Snapshot Observer for File and CDVD Subsystems

**Files:**
- Create: `game_hooks/src/file_snapshot.hpp`, `tests/cpp/file_snapshot_tests.cpp`
- Modify: `game_hooks/src/aoe2_call_trace.cpp`, `CMakeLists.txt`

**Interfaces:**
- Produces: `ObserveFileState(uint8_t* rdram, uint64_t step)` and `ObserveCdvdState(uint8_t* rdram, uint64_t step)` that emit `[state-snapshot] label=file` and `[state-snapshot] label=cdvd` capturing `CdServerState` (`D_4F1508`) and `CdStreamStatus` (`D_4F16C0`).

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/file_snapshot_tests.cpp
#include "file_snapshot.hpp"
#include <vector>
// tests formatting of kFileRegion and kCdvdRegion
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_file_snapshot_tests`
Expected: FAIL with compilation error (file_snapshot.hpp not found).

- [x] **Step 3: Implement snapshot observers in `game_hooks/src/file_snapshot.hpp` and wire into `aoe2_call_trace.cpp`**

Emit FNV-1a hash and byte hex representations of `CdServerState` (20 bytes) and `CdStreamStatus` (12 bytes) at logical frame intervals.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R file_snapshot_tests --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add game_hooks/src/file_snapshot.hpp game_hooks/src/aoe2_call_trace.cpp tests/cpp/file_snapshot_tests.cpp
git commit -m "feat: file and cdvd state snapshot observer"
```

---

### Task 3: Pure In-Memory File Table Model (`KtFileModel`)

**Files:**
- Create: `file/include/aoe2/file/kt_file_model.hpp`, `file/src/kt_file_model.cpp`, `tests/cpp/kt_file_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `aoe2::runtime::GuestAccess`
- Produces: `class KtFileModel`
  - `uint32_t SetRecordTable(uint32_t table_addr)`
  - `uint32_t SetEntryTable(uint32_t table_addr)`
  - `int32_t ClassifyKind(uint32_t arg_addr)`
  - `uint32_t GetHandlerByKind(int32_t kind)`
  - `int32_t BinarySearch(int32_t id, uint32_t table_addr, int32_t count)`
  - `int32_t FindNamedEntry(const char* name)`

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/kt_file_model_tests.cpp
// tests BinarySearch, ClassifyKind, SetRecordTable
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_kt_file_model_tests`
Expected: FAIL with missing header.

- [x] **Step 3: Implement `KtFileModel` in `file/src/kt_file_model.cpp`**

Implement record table parsing, binary search (`func_0012D128`), entry table lookup, and dispatch handlers faithfully following `src/kcet/kt_file.c` and decomp specs.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R kt_file_model_tests --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add file/include/aoe2/file/kt_file_model.hpp file/src/kt_file_model.cpp tests/cpp/kt_file_model_tests.cpp
git commit -m "feat: native kt_file model over guest memory"
```

---

### Task 4: Host Disc and CD/DVD Server Model (`CdvdModel`)

**Files:**
- Create: `cdvd/include/aoe2/cdvd/cdvd_model.hpp`, `cdvd/src/cdvd_model.cpp`, `tests/cpp/cdvd_model_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: `aoe2::runtime::GuestAccess`
- Produces: `class CdvdModel`
  - `bool Initialize(std::string_view disc_root)`
  - `int32_t ReadSectors(uint32_t sector_lba, uint32_t sector_count, uint32_t dest_guest_addr)`
  - `int32_t GetStreamStatus(uint32_t status_addr)`
  - `void ProcessPendingRequests()`
  - `int32_t Reset()`

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/cdvd_model_tests.cpp
// tests initialization, status, QueueRead, ProcessPendingRequests
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_cdvd_model_tests`
Expected: FAIL with missing header.

- [x] **Step 3: Implement `CdvdModel` in `cdvd/src/cdvd_model.cpp`**

Implement sector resolution against host disc root (`game/SLES-50282/`), stream queue management, and status updates for `D_4F16C0`.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R cdvd_model_tests --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add cdvd/include/aoe2/cdvd/cdvd_model.hpp cdvd/src/cdvd_model.cpp tests/cpp/cdvd_model_tests.cpp
git commit -m "feat: native cdvd model for host disc access"
```

---

### Task 5: Native Module Adapters, Shared Services, and Switches

**Files:**
- Create: `game_hooks/src/kt_file_entries.inc`, `game_hooks/src/kt_cdvd_entries.inc`, `game_hooks/src/kcet_file_module.cpp`, `game_hooks/src/kcet_cdvd_module.cpp`
- Modify: `game_hooks/src/native_module_switches.cpp`, `cmake/Aoe2Recompiled.cmake`

**Interfaces:**
- Produces: `RegisterFileHandlers(KcetHandlers&)` and `RegisterCdvdHandlers(KcetHandlers&)` activated by `file=on` and `cdvd=on` in `native_modules.ini`.
- Uses `PS2_REGISTER_GAME_OVERRIDE` to replace guest functions. Handlers set `ctx->pc = getRegU32(ctx, 31)` and `setReturnU32(ctx, result)`.

- [x] **Step 1: Write the failing test**

```cpp
// tests/cpp/native_file_adapter_tests.cpp
// Test argument mapping and return value conventions
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target aoe2_native_file_adapter_tests`
Expected: FAIL.

- [x] **Step 3: Implement adapters and register overrides**

Implement X-macro tables in `kt_file_entries.inc` and `kt_cdvd_entries.inc`. Bind handlers in `kcet_file_module.cpp` and `kcet_cdvd_module.cpp`.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R native_file_adapters --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add game_hooks/src/kt_file_entries.inc game_hooks/src/kt_cdvd_entries.inc game_hooks/src/kcet_file_module.cpp game_hooks/src/kcet_cdvd_module.cpp
git commit -m "feat: native file and cdvd module adapters behind switches"
```

---

### Task 6: Differential Testing of Pure Memory Routines

**Files:**
- Create: `tests/cpp/file_differential_tests.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: Recompiled original functions and native `KtFileModel` methods.
- Produces: Differential tests comparing outputs for binary search, kind classification, and record table indexing across identical synthetic guest memory states.

- [x] **Step 1: Write failing differential tests**

```cpp
// tests/cpp/file_differential_tests.cpp
// Compare original recompiled function against native model on identical input vectors
```

- [x] **Step 2: Run test to verify execution**

Run: `cmake --build build --target aoe2_file_differential_tests`
Expected: Verify compilation and execution.

- [x] **Step 3: Implement test vectors and edge cases**

Cover 0 records, 1 record, max records, duplicate keys, null pointers, and out-of-range kinds.

- [x] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build -R file_differential --output-on-failure`
Expected: PASS

- [x] **Step 5: Commit**

```bash
git add tests/cpp/file_differential_tests.cpp
git commit -m "test: differential verification of pure file routines"
```

---

### Task 7: Original vs Native Snapshot Comparison to Observed Parity

**Files:**
- Modify: `docs/sot/file-cdvd-semantics.md`, `docs/sot/kcet-stall-log.md`, `docs/superpowers/plans/2026-10-25-native-cdvd-file-modules.md`

- [x] **Step 1: Rebuild game and run 30s original baseline capture**

Run: `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1 python3 tools/validate_desktop.py --run-seconds 30 --interval 5` (`file=off, cdvd=off`)
Save log to `run-file-orig.log`.

- [x] **Step 2: Enable `file=on` and `cdvd=on` in `native_modules.ini` and run 30s native capture**

Run: `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1 python3 tools/validate_desktop.py --run-seconds 30 --interval 5` (`file=on, cdvd=on`)
Save log to `run-file-native.log`.

- [x] **Step 3: Compare snapshots byte for byte**

Compare all `label=file` and `label=cdvd` snapshot lines across both runs. Confirm matching FNV checksums and byte buffers. 34/34 snapshots matched byte-for-byte.

- [x] **Step 4: Record observed parity, bounds, and limits**

Update documentation with snapshot counts, exact checksum matches, and remaining gaps. Recorded in `docs/sot/file-cdvd-semantics.md` and `docs/sot/kcet-stall-log.md`.

- [x] **Step 5: Commit**

```bash
git add docs/sot/ docs/superpowers/plans/2026-10-25-native-cdvd-file-modules.md
git commit -m "docs: record native file and cdvd observed parity"
```

---

## Self-Review

1. **Spec Coverage:** Implements `docs/superpowers/specs/2026-10-23-file-module-native-shim-design.md` and fulfills item 4 of `docs/superpowers/specs/2026-10-24-kcet-native-layer-design.md`.
2. **Step Scan:** Every task follows strict TDD (failing test first, implementation, verification, commit).
3. **Type Consistency:** Standardized on `GuestAccess`, `RdramGuestAccess`, and the `CdServerState`/`CdStreamStatus` layouts defined in `include/kt_file.h`.
4. **Review Focus:** Explicitly tests binary search bounds, kind classification, stream status updates, disc file path safety, and deterministic snapshot capture.
5. **Proportion:** Clear, bite-sized tasks structured identically to the completed pad, mouse, and keyboard plans.
