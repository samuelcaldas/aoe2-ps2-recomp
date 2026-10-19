# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` and `GEMINI.md` are symlinks to this file — edits here apply to all agents.

## What this is

Bootstrap foundation for a native static-recompilation port of *Age of Empires II: The Age of Kings*, PS2 PAL (`SLES-50282`, `AOK.ELF`, SHA-1 `f478e19f2059d52899b691483ef5c7522e30e17c`). Target order: desktop Linux x86-64 → ARM64 portability gate → Nintendo Switch homebrew (libnx + deko3d). Nothing is playable yet; the generated C++ has not been compiled against a runtime yet. Next milestone (per `docs/ghidra-ps2recomp-workflow.md`): compile the generated output against a minimal desktop runtime probe and classify the first blockers with `tools/classify_diagnostics.py` — **without adding game-specific stubs**.

## Commands

```bash
# Python tests (stdlib unittest; run from repo root — tests import `core.*` / `tools.*` as packages)
python3 -m unittest discover -s tests
python3 -m unittest tests.test_diagnostic_classifier                       # one module
python3 -m unittest tests.test_diagnostic_classifier.TestDiagnosticClassifier.test_missing_host_dependency  # one test

# C++ host scaffolding (C++20, CMake >= 3.20, Ninja)
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure        # single test: smoke_test

# Dependencies / provenance
python3 tools/bootstrap_dependencies.py [--check]  # clone third_party/ at lockfile SHAs
python3 tools/import_owned_game.py --source /path/to/AOK.ELF
python3 tools/verify_owned_game.py
python3 tools/validate_ghidra_env.py               # needs GHIDRA_HOME
python3 tools/run_ps2recomp_pipeline.py            # Ghidra export + PS2Recomp codegen + manifest
python3 tools/verify_generation_manifest.py
python3 tools/classify_diagnostics.py --log build.log [--out report.json]
```

No linter is configured. Style is set by `.editorconfig` (4-space indent, 2 for JSON/YAML, LF).

## Architecture

Pipeline: user-owned `AOK.ELF` → Ghidra headless (`MIPS:LE:64:64-32addr`) + PS2Recomp's `ExportPS2Functions.java` → `analysis/generated/function_map.csv` + `SLES-50282.toml` → `ps2_recomp` → `generated/SLES-50282/*.cpp` (~17k files, one per function) → host runtime/platform layers.

Layers (C++ namespaces `aoe2::<layer>`, headers under `<layer>/include/aoe2/<layer>/`, all compiled into the single static lib `aoe2_interfaces` in `CMakeLists.txt`):
- `core/` — single source of truth for target metadata. `core/target.py` (`SINGLE_TARGET`) is imported by every Python tool; `core/include/aoe2/core/target.hpp` mirrors it for C++. Keep both in sync. No host OS dependencies.
- `runtime/` — `GuestMemory` (32 MB EE RAM) and the abstract `PlatformServices` (logging, clock).
- `platform/desktop/` — concrete `PlatformServices` + `ActionRecorderInputSink`; `src/main.cpp` is the `aoe2_smoke_test` ctest executable. Future `platform/switch/`.
- `input/` — platform-neutral `ActionType`/`ActionEvent`/`InputSink` (pointer + menu actions; Switch touch/controller maps onto these later).
- `renderer/` — `IRenderer` + `NullRenderer` (headless). Design mandate: replace rendering at the high level (sprite/tile/UI draws mapped to `IRenderer`), **not** low-level GS emulation.
- `tools/` — Python 3 stdlib-only, fail-fast scripts; each resolves `REPO_ROOT` and inserts it into `sys.path` to import `core.target`.

See `docs/architecture.md` (layer rationale, EE/VU/GS/IOP notes) and `docs/ghidra-ps2recomp-workflow.md` (exact pipeline commands, TOML config, manifest schema).

## Legal / provenance rules (enforced by tooling — do not weaken)

- Never commit game binaries, ISOs, Ghidra projects, function maps, or generated C++. `game/`, `analysis/{generated,ghidra,exploratory_fallback}/`, `generated/`, `third_party/`, `*.ELF/*.bin/*.iso` are gitignored; `verify_generation_manifest.py` checks every manifest artifact path with `git check-ignore`.
- Tracked metadata (`generation_manifest.json`, reports from `classify_diagnostics.py`) must contain only hashes/counts/versions — no disassembly, function names, hex addresses, or code snippets.
- The function map must come from Ghidra (`function_map_source: "ghidra_export"`). The PS2Recomp `ps2_analyzer` fallback is for local bring-up only; the manifest verifier rejects `fallback_analyzer`, and `run_ps2recomp_pipeline.py` stops if Ghidra is missing rather than substituting it.
- Clean-room: no code, headers, or symbols from unofficial disassembly projects.
- Third-party deps are pinned in `third_party_provenance.lock.json` (PS2Recomp is GPL-3.0 — runtime code linking it inherits GPL obligations; libnx ISC; deko3d Zlib). Bump SHAs only via `bootstrap_dependencies.py`.
- The PS2Recomp TOML stays minimal: no speculative `stubs`, `skip`, or instruction patches.
