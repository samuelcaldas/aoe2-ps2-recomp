# Desktop Runtime Compilation and Link Probe Specification

## 1. Overview & Objectives

This document records the design, execution, and factual diagnostic outcomes of the minimal desktop x86-64/Linux compile-and-link probe for the authoritative PS2Recomp output of **Age of Empires II: The Age of Kings** (`SLES-50282`, `AOK.ELF`).

The primary objectives of this phase:
1. **Full-Scope Inclusion**: Incorporate all 17,421 authoritative generated translation units (`generated/SLES-50282/*.cpp`) and `register_functions.cpp` into a reproducible desktop compilation target.
2. **Strict Non-Execution**: Ensure the probe binary is passive and strictly diagnostic. The probe entry point does **not** call guest functions, emulate hardware, or boot the game.
3. **No Speculative Modifications**: Adhere to clean-room and integrity boundaries by introducing zero `ret0`, `ret1`, `reta0`, `skip`, instruction patches, fake syscall tables, or game-specific stubs.
4. **Factual Classification**: Identify and categorize genuine compiler, linker, runtime API, and host dependency blockers using automated, sanitized diagnostic reporting (`tools/classify_diagnostics.py`).

---

## 2. Preflight and Provenance Verification

Prior to conducting the probe, all prerequisites and authoritative inputs were verified:

```bash
python3 tools/verify_owned_game.py
python3 tools/bootstrap_dependencies.py --check
python3 tools/verify_generation_manifest.py
```

* **Target ELF**: `game/SLES-50282/AOK.ELF` (SHA-1: `f478e19f2059d52899b691483ef5c7522e30e17c`, Size: 4,076,904 bytes).
* **Generation Manifest**: `generation_manifest.json` (`function_map_source: "ghidra_export"`).
* **PS2Recomp Revision**: `eb8688b2fd191129f103a39543002c005b33409c` (pinned in `third_party_provenance.lock.json`).
* **Toolchain**:
  - Compiler: GCC 15.2.0 (`/usr/bin/c++`, C++20 standard).
  - Linker: GNU ld (GNU Binutils) 2.46.
  - Build System: CMake 4.2.3, Ninja 1.13.2.
  - Host OS / Architecture: Linux x86_64.

---

## 3. Probe Architecture & Discovery

The compilation probe is integrated into `CMakeLists.txt` via the option `AOE2_BUILD_DESKTOP_PROBE`:

* **Target Name**: `desktop_runtime_probe`
* **Entry Point**: `platform/desktop/src/desktop_runtime_probe.cpp`
* **Source Discovery**: Automatically globs all 17,421 generated `.cpp` files in `generated/SLES-50282/` into an OBJECT library `aoe2_generated_game_code`.
* **Include Directories**:
  - `generated/SLES-50282`
  - `third_party/PS2Recomp/ps2xRuntime/include`
  - `third_party/PS2Recomp/ps2xRuntime/src/lib/Kernel` (providing `Stubs/Unimplemented.h` referenced by `ps2_stubs.h`)
  - `third_party/PS2Recomp/ps2xIOP/include`
* **Execution Safety**: The probe entry point logs toolchain metadata and registration slot counts without invoking any recompiled guest entry points.

---

## 4. Factual Diagnostic Classification

A complete probe execution was run to evaluate compiler and linker blockers across the recompiled code set. All output was captured in `build/desktop-probe/raw_diagnostics.log` (git-ignored) and sanitized via:

```bash
python3 tools/classify_diagnostics.py \
    --log build/desktop-probe/raw_diagnostics.log \
    --out build/desktop-probe/classified_report.json
```

### 4.1 Summary of Findings

| Blocker Category | Count | Status / Analysis |
| :--- | :--- | :--- |
| **`missing_ps2recomp_runtime_api`** | 14,478 | **Primary Blocker**: Missing runtime implementations of member functions on `PS2Runtime` (e.g. `PS2Runtime::Store128`, `PS2Runtime::dispatchGuestBranch`, `g_ps2RecompiledFunctionTableSlotCount`) and missing host backend integration (`ps2_runtime` static library). |
| **`unresolved_generated_symbol`** | 1 | Unresolved callsites across translation units or table registrations (e.g. guest function callsites). |
| **`linker_or_build_graph`** | 1 | Linker termination status (`collect2: error: ld returned 1 exit status`). |
| **`generated_source_syntax_or_codegen`** | 0 | **No syntax or C++20 standard errors**: Generated code compiles cleanly with GCC 15.2.0 under `-std=c++20`. |
| **`host_compiler_or_standard_library_incompatibility`**| 0 | No C++ standard library incompatibilities. |
| **`host_vector_or_simd_portability`** | 0 | No vector instruction mismatches on x86-64. |
| **`resource_or_toolchain_limit`** | 0 | Ninja and GCC successfully process translation units without OOM or disk exhaustion. |

### 4.2 Sanitized Diagnostic Sample

```json
{
  "category_counts": {
    "missing_ps2recomp_runtime_api": 14478,
    "unresolved_generated_symbol": 1,
    "linker_or_build_graph": 1
  },
  "sanitized_samples": {
    "missing_ps2recomp_runtime_api": [
      "desktop_runtime_probe.cpp:(.text+<HEX_ADDR>): referência não definida para \"g_ps2RecompiledFunctionTableSlotCount\"",
      "<GUEST_FUNC><HEX_ADDR>.cpp:(.text+<HEX_ADDR>): referência não definida para \"PS2Runtime::Store128(unsigned char*, R5900Context*, unsigned int, long long __vector(2))\"",
      "<PATH>/x86_64-linux-gnu-ld.bfd: <GUEST_FUNC><HEX_ADDR>.cpp:(.text+<HEX_ADDR>): referência não definida para \"PS2Runtime::dispatchGuestBranch(unsigned char*, R5900Context*, unsigned int, unsigned int, unsigned int, PS2Runtime::GuestBranchKind, char const*)\""
    ],
    "unresolved_generated_symbol": [
      "<GUEST_FUNC><HEX_ADDR>.cpp:(.text+<HEX_ADDR>): referência não definida para \"<GUEST_FUNC><HEX_ADDR>(unsigned char*, R5900Context*, PS2Runtime*)\""
    ],
    "linker_or_build_graph": [
      "collect2: error: ld returned 1 exit status"
    ]
  },
  "total_diagnostics_classified": 14480
}
```

---

## 5. Architectural Recommendations & Next Steps

1. **Host Dependencies for `ps2xRuntime`**:
   The upstream PS2Recomp desktop runtime (`third_party/PS2Recomp/ps2xRuntime`) depends on `raylib` and `ffmpeg` (`libavcodec`, `libavformat`, `libswscale`) for its default host backend. These host libraries or our own lightweight headless runtime backend will be needed to satisfy `PS2Runtime` symbol requirements.
2. **Preserve Clean-Room Mandate**:
   Never patch generated functions or add stub functions with fabricated return codes (`ret0`/`ret1`). All runtime dispatch and memory access functions (`Store128`, `dispatchGuestBranch`, `Read128`, etc.) must be provided by the runtime layer (`aoe2::runtime` or `ps2xRuntime`).
3. **Linker Probe Baseline**:
   With the diagnostic classifier established and passing 27 automated tests, future build experiments can continuously measure reduction in missing runtime API references as the runtime abstraction is fleshed out.
