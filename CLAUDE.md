# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` and `GEMINI.md` are symlinks to this file — edits here apply to all agents.

## What this is

Bootstrap foundation for a native static-recompilation port of *Age of Empires II: The Age of Kings*, PS2 PAL (`SLES-50282`, `AOK.ELF`, SHA-1 `f478e19f2059d52899b691483ef5c7522e30e17c`). Primary target: Nintendo Switch homebrew (libnx + deko3d). Bring-up order: desktop Linux x86-64 (validation host) → ARM64 portability gate → Switch. Nothing is playable yet. The full recompilation compiles and links on desktop x86-64: all generated units plus the pinned upstream PS2Recomp runtime build `desktop_runtime_probe` (see `docs/desktop-runtime-probe.md` §6), which has never executed guest code. It initializes headless (memory, subsystems, ELF load, entry point resolved; `ctest -R desktop_runtime_probe_init`) without calling `run()`. Next milestone: the ELF loads code overlays from disc at run time (eight executable PT_LOAD segments with `filesz = 0`, see `docs/desktop-runtime-probe.md` §7.1); identify them and recompile them through the same guarded pipeline — **without adding game-specific stubs**. Build it only with `python3 tools/run_desktop_probe_build.py --jobs N` (memory-capped systemd scope, C locale).

## Commands

```bash
# Python tests (stdlib unittest; run from repo root — tests import `core.*` / `tools.*` as packages)
python3 -m unittest discover -s tests
python3 -m unittest tests.test_diagnostic_classifier                       # one module
python3 -m unittest tests.test_diagnostic_classifier.TestDiagnosticClassifier.test_missing_host_dependency  # one test

# C++ host scaffolding (C++20, CMake >= 3.20, Ninja)
cmake -B build -G Ninja && cmake --build build
ctest --test-dir build --output-on-failure        # tests: smoke_test, input_tests
ctest --test-dir build -R input_tests --output-on-failure  # single test

# Dependencies / provenance
python3 tools/bootstrap_dependencies.py [--check]  # clone third_party/ at lockfile SHAs
python3 tools/import_owned_game.py --source /path/to/AOK.ELF
python3 tools/verify_owned_game.py
python3 tools/validate_ghidra_env.py               # needs GHIDRA_HOME
python3 tools/run_ps2recomp_pipeline.py            # Ghidra export + PS2Recomp codegen + manifest
python3 tools/verify_generation_manifest.py
python3 tools/verify_function_map.py               # reject map entries that span data (runs inside the pipeline)
python3 tools/install_ghidra_ee_extension.py       # pinned Emotion Engine (R5900) language; the pipeline requires it
python3 tools/import_owned_disc.py --iso /path/to/aoe2.iso  # extract the owned disc next to AOK.ELF (CD root)
python3 tools/resolve_kcet_hooks.py                # KCET hook addresses from ../aoe decomp -> ignored game/SLES-50282/kcet_hooks.ini
python3 tools/docker_build.py --platform desktop|android|switch [--jobs N]  # containerized builds (docker-dev)
python3 tools/classify_diagnostics.py --log build.log [--out report.json]
```

No linter is configured. Style is set by `.editorconfig` (4-space indent, 2 for JSON/YAML, LF).

## Architecture

Pipeline: user-owned `AOK.ELF` → Ghidra headless (`MIPS:LE:64:64-32addr`) + PS2Recomp's `ExportPS2Functions.java` → `analysis/generated/function_map.csv` + `SLES-50282.toml` → `ps2_recomp` → `generated/SLES-50282/*.cpp` (~17k files, one per function) → host runtime/platform layers.

Layers (C++ namespaces `aoe2::<layer>`, headers under `<layer>/include/aoe2/<layer>/`, all compiled into the single static lib `aoe2_interfaces` in `CMakeLists.txt`):
- `core/` — single source of truth for target metadata. `core/target.py` (`SINGLE_TARGET`) is imported by every Python tool; `core/include/aoe2/core/target.hpp` mirrors it for C++. Keep both in sync. No host OS dependencies.
- `runtime/` — `GuestMemory` (32 MB EE RAM) and the abstract `PlatformServices` (logging, clock).
- `platform/desktop/` — concrete `PlatformServices` + `ActionRecorderInputSink`; `src/main.cpp` is the `aoe2_smoke_test` ctest executable. Future `platform/switch/`.
- `input/` — platform-neutral `ActionType`/`ActionEvent`/`InputSink`. The PS2 game supports a USB mouse; on Switch handheld it is replaced by the touchscreen: `TouchGestureTranslator` (+ `SurfaceMapping`, host → 640x480 guest pixels) turns touches into pointer actions, and `GuestMouseAdapter` folds them into the per-frame mouse state the recompiled game polls. Docked, a USB mouse and keyboard on the dock mirror the PS2 peripherals: `UsbMouseTranslator` (relative reports → the same pointer actions) and `GuestKeyboardAdapter` (USB HID boot-protocol reports); `SourcesFor(OperationMode)` picks the sources per mode. No USB/IOP/SIF emulation (see `docs/architecture.md` §6). Replacements hook the game's KCET hardware layer (`kt_mouse`, `kt_keybd`, `kt_gpu`, ...) via runtime function replacement, with guest addresses resolved into ignored files — see `docs/adr/0001-kcet-boundary-replacement.md`.
- `renderer/` — `IRenderer` + `NullRenderer` (headless). Design mandate: replace rendering at the high level (sprite/tile/UI draws mapped to `IRenderer`), **not** low-level GS emulation.
- `tools/` — Python 3 stdlib-only, fail-fast scripts; each resolves `REPO_ROOT` and inserts it into `sys.path` to import `core.target`.

See `docs/architecture.md` (layer rationale, EE/VU/GS/IOP notes) and `docs/ghidra-ps2recomp-workflow.md` (exact pipeline commands, TOML config, manifest schema).

`docs/sot/prior-art-and-reuse.md` is the source of truth for prior art and what may be reused: PS2Recomp already supports ARM through sse2neon (`USE_SSE2NEON`; the ARM64 gate should reuse it, not add a SIMD layer), the God of War native runtime (AOT VU1/IRX, native GS) via the GPL-3.0 `KIexster/god-of-war-recomp`, and `SwitchWakerHD` (MPL-2.0, libnx + deko3d) as the Switch backend model. Check each source's license there before copying code. It also records the graphics decision (OpenGL ES 3 via devkitPro's Mesa on Switch, one GLES backend shared with desktop; no Vulkan; deko3d only if profiling demands) and that **devkitPro runs only in Docker** (`devkitpro/devkita64` on the `docker-dev` context, repo mounted), never on the host.

## Legal / provenance rules (enforced by tooling — do not weaken)

- Never commit game binaries, ISOs, Ghidra projects, function maps, or generated C++. `game/`, `analysis/{generated,ghidra,exploratory_fallback}/`, `generated/`, `third_party/`, `*.ELF/*.bin/*.iso` are gitignored; `verify_generation_manifest.py` checks every manifest artifact path with `git check-ignore`.
- Tracked metadata (`generation_manifest.json`, reports from `classify_diagnostics.py`) must contain only hashes/counts/versions — no disassembly, function names, hex addresses, or code snippets.
- The ELF is stripped and loads code and data in one RWX segment. `tools/ghidra/MarkDataRegions.java` runs before the exporter and makes return-free regions (> 64 KiB without `jr $ra`) non-executable; `tools/verify_function_map.py` must report zero violations before generation. Fix oversized or data-spanning entries in the Ghidra step, never by editing generated C++ or adding TOML skips.
- The function map must come from Ghidra (`function_map_source: "ghidra_export"`). The PS2Recomp `ps2_analyzer` fallback is for local bring-up only; the manifest verifier rejects `fallback_analyzer`, and `run_ps2recomp_pipeline.py` stops if Ghidra is missing rather than substituting it.
- Clean-room: no code, headers, or symbols from unofficial disassembly projects. **Exception authorized by the owner (updated 2026-10-24):** their matching AoE2 decompilation (`samuelcaldas/age-of-empires-2-decomp`, local `../aoe`) is the owner's own work and **may be copied directly into tracked files of this repository** (source, headers, names, addresses and sidecar metadata taken from it), for any subsystem: KCET, mouse, keyboard, network, graphics, and others. Its C/C++ may stand in for code that the pipeline generated from the binary, through host-side override hooks (ADR 0001; `docs/sot/multiplayer-lan-architecture.md`), be compiled for the host for debugging, or replace PS2-only code. Mark such files with their origin (decomp repo, commit). **Still never tracked:** game binaries, ISOs, Ghidra projects and exports, function maps, disassembly produced from the binary, and generated C++. Do not modify the decomp repository from here. Generated C++ is never edited in place. Rules in `docs/sot/prior-art-and-reuse.md` §4.
- Third-party deps are pinned in `third_party_provenance.lock.json` (PS2Recomp is GPL-3.0 — runtime code linking it inherits GPL obligations; libnx ISC; deko3d Zlib). Bump SHAs only via `bootstrap_dependencies.py`.
- The PS2Recomp TOML stays minimal: no speculative `stubs`, `skip`, or instruction patches.
