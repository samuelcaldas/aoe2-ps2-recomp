# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

`AGENTS.md` and `GEMINI.md` are symlinks to this file — edits here apply to all agents.

## What this is & Project Philosophy

Bootstrap foundation for a native static-recompilation port of *Age of Empires II: The Age of Kings*, PS2 PAL (`SLES-50282`, `AOK.ELF`, SHA-1 `f478e19f2059d52899b691483ef5c7522e30e17c`). Primary target: Nintendo Switch homebrew (libnx + deko3d). Bring-up order: desktop Linux x86-64 (validation host) → ARM64 portability gate → Switch. Nothing is playable yet. The full recompilation compiles and links on desktop x86-64: all generated units plus the pinned upstream PS2Recomp runtime build `desktop_runtime_probe` (see `docs/desktop-runtime-probe.md` §6), which has never executed guest code. It initializes headless (memory, subsystems, ELF load, entry point resolved; `ctest -R desktop_runtime_probe_init`) without calling `run()`. Next milestone: the ELF loads code overlays from disc at run time (eight executable PT_LOAD segments with `filesz = 0`, see `docs/desktop-runtime-probe.md` §7.1); identify them and recompile them through the same guarded pipeline — **without adding game-specific stubs**. Build it only with `python3 tools/run_desktop_probe_build.py --jobs N` (memory-capped systemd scope, C locale).

### 1. Why PS2 Recompilation instead of PC Decompilation?
- **Console-Native Architecture & Binary Structure**: The PS2 port was engineered by KCET (Konami Computer Entertainment Tokyo). The binary is a self-contained, statically-linked MIPS ELF (`AOK.ELF`) with a clean abstraction layer (the `kt_*` hardware libraries for graphics, sound, file I/O, memory card, and peripherals). Unlike the 1999 PC Win32 release—which is tightly bound to legacy DirectDraw, Win32 message loops, legacy GDI, DirectPlay, and x86 inline assembly—the PS2 codebase was already structured for a closed console runtime.
- **Console-Tailored UI & Input Schemes**: The PS2 version is the only classic edition of Age of Empires II with native gamepad navigation (DualShock 2 pad maps, radial/context menus, console shortcuts, and auto-targeting) while also retaining native USB keyboard and mouse support. For handheld devices like the Nintendo Switch, having an interface and gameplay loop already designed and balanced for controllers and fixed-resolution displays is infinitely superior to retrofitting a 1999 mouse-only Win32 desktop UI.

### 2. Recompilation & Decompilation Hand-in-Hand
- **Surgical Replacement of Architectural Bottlenecks**: Recompilation and decompilation are not mutually exclusive; they work together. PS2Recomp automatically translates the vast majority (~17,000 game logic functions) into C++ without human error or years of manual decompilation.
- **Native Host Subsystem Overrides**: Portions of the PS2 code constrained by hardware limitations (the 32 MB EE RAM boundary, KCET GS software rasterization blitters, low-level IOP/RPC calls, and CDVD streaming) are replaced surgically with native host implementations based directly on the matching decompilation (`samuelcaldas/age-of-empires-2-decomp`, local `../aoe`). The game's KCET layer (`kt_gpu`, `kt_mouse`, `kt_keybd`, `kt_file`, `kt_sound`) serves as the ideal boundary: host-side C++ interceptors override KCET entry points with modern hardware-accelerated equivalents (e.g. OpenGL ES 3 / deko3d rendering, native filesystem access, and Horizon OS touch/Joy-Con input).

### 3. HD Assets Strategy & High-Resolution Presentation
- **Asset Provenance & Scaling Realities**: The 2013 "HD Edition" on PC used the exact same 1999 sprite assets (`~85×56` px units, `800×600` UI art, 1:1 SLP tiles). The PS2 version rendered a letterboxed 640×262 software playfield at $y=40$ within a 640×448 logical framebuffer (PAL stretched to 640×512).
- **Engine Viewport Expansion**: Achieving a crisp, native HD/1080p experience on modern displays (desktop and docked Switch) does **not** mean naive upscaling or replacing 1:1 sprites with distorted textures. Following the canonical strategy ([`docs/sot/hd-assets-strategy.md`](docs/sot/hd-assets-strategy.md)), the engine's internal map viewport is expanded (rendering more map area at 1:1 native scale), while HUD elements are anchored cleanly to display borders and UI screens use high-resolution assets or clean aspect-ratio preservation.

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
- `input/` — platform-neutral `ActionType`/`ActionEvent`/`InputSink`. The PS2 game supports a USB mouse; on Switch handheld it is replaced by the touchscreen: `TouchGestureTranslator` (+ `SurfaceMapping`, host → 640x448 guest logical pixels) turns touches into pointer actions, and `GuestMouseAdapter` folds them into the per-frame mouse state the recompiled game polls. Docked, a USB mouse and keyboard on the dock mirror the PS2 peripherals: `UsbMouseTranslator` (relative reports → the same pointer actions) and `GuestKeyboardAdapter` (USB HID boot-protocol reports); `SourcesFor(OperationMode)` picks the sources per mode. No USB/IOP/SIF emulation (see `docs/architecture.md` §6). Replacements hook the game's KCET hardware layer (`kt_mouse`, `kt_keybd`, `kt_gpu`, ...) via runtime function replacement, with guest addresses resolved into ignored files — see `docs/adr/0001-kcet-boundary-replacement.md`.
- `renderer/` — `IRenderer` + `NullRenderer` (headless). Design mandate: replace rendering at the high level (sprite/tile/UI draws mapped to `IRenderer`), **not** low-level GS emulation.
- `tools/` — Python 3 stdlib-only, fail-fast scripts; each resolves `REPO_ROOT` and inserts it into `sys.path` to import `core.target`.

## Documentation Chain & Canonical References

Every document in the repository links into a coherent chain leading back to this file (see [`docs/README.md`](docs/README.md) for the complete documentation index and visual hierarchy):

- **System Architecture**: [`docs/architecture.md`](docs/architecture.md) (subsystems, EE/VU/GS/IOP notes, and layer boundaries).
- **Nintendo Switch Target**: [`docs/switch-target-architecture.md`](docs/switch-target-architecture.md) (Horizon OS services, `appletHook` 720p/1080p scaling, `audren` audio, Title Takeover memory protection, and touch/Joy-Con input).
- **HD Asset Strategy & Viewport**: [`docs/sot/hd-assets-strategy.md`](docs/sot/hd-assets-strategy.md) (canonical SOT for HD 2013 assets, runtime playfield rasterization, and viewport expansion).
- **KCET Native Boundary**: [`docs/adr/0001-kcet-boundary-replacement.md`](docs/adr/0001-kcet-boundary-replacement.md) (ADR governing the runtime replacement of the game's hardware abstraction layer).
- **Prior Art, Reuse & Permissions**: [`docs/sot/prior-art-and-reuse.md`](docs/sot/prior-art-and-reuse.md) (provenance, clean-room rules, dependency licenses, and authorized decompilation reuse).
- **Recompilation Workflow**: [`docs/ghidra-ps2recomp-workflow.md`](docs/ghidra-ps2recomp-workflow.md) (Ghidra headless export, TOML configuration, and PS2Recomp codegen).
- **Desktop Probe & Overlays**: [`docs/desktop-runtime-probe.md`](docs/desktop-runtime-probe.md) (Linux host probe and dynamic code overlay milestones).
- **Active Implementation Plan**: [`docs/plans/nifty-herding-pudding.md`](docs/plans/nifty-herding-pudding.md) (Phase A–D: configurable engine viewport, 1:1 playfield presentation, and border-anchored HUD recomposition).

## Legal / provenance rules (enforced by tooling — do not weaken)

- Never commit game binaries, ISOs, Ghidra projects, function maps, or generated C++. `game/`, `analysis/{generated,ghidra,exploratory_fallback}/`, `generated/`, `third_party/`, `*.ELF/*.bin/*.iso` are gitignored; `verify_generation_manifest.py` checks every manifest artifact path with `git check-ignore`.
- Tracked metadata (`generation_manifest.json`, reports from `classify_diagnostics.py`) must contain only hashes/counts/versions — no disassembly, function names, hex addresses, or code snippets.
- The ELF is stripped and loads code and data in one RWX segment. `tools/ghidra/MarkDataRegions.java` runs before the exporter and makes return-free regions (> 64 KiB without `jr $ra`) non-executable; `tools/verify_function_map.py` must report zero violations before generation. Fix oversized or data-spanning entries in the Ghidra step, never by editing generated C++ or adding TOML skips.
- The function map must come from Ghidra (`function_map_source: "ghidra_export"`). The PS2Recomp `ps2_analyzer` fallback is for local bring-up only; the manifest verifier rejects `fallback_analyzer`, and `run_ps2recomp_pipeline.py` stops if Ghidra is missing rather than substituting it.
- Clean-room: no code, headers, or symbols from unofficial disassembly projects. **Owner decompilation authorized:** the owner's matching AoE2 decompilation (`samuelcaldas/age-of-empires-2-decomp`, local `../aoe`) is the owner's own work and **may be copied directly into tracked files of this repository** (source, headers, names, addresses, and sidecar metadata), for any subsystem (KCET, mouse, keyboard, network, graphics, etc.). Its C/C++ may stand in for code that the pipeline generated from the binary (via host-side override hooks), be compiled for the host for debugging, or replace PS2-only code. Mark such files with their origin (decomp repo, commit). **Still never tracked:** game binaries, ISOs, Ghidra projects and exports, function maps, disassembly produced from the binary, and generated C++. Do not modify the decomp repository from here. Generated C++ is never edited in place. Rules in `docs/sot/prior-art-and-reuse.md` §4.
- Third-party deps are pinned in `third_party_provenance.lock.json` (PS2Recomp is GPL-3.0 — runtime code linking it inherits GPL obligations; libnx ISC; deko3d Zlib). Bump SHAs only via `bootstrap_dependencies.py`.
- **Game-Specific PS2Recomp Modifications & Forking Authorized:** The user explicitly requires a customized PS2Recomp runtime tailored for this game's exact compatibility (IOP handshakes, overlay dispatch, CDVD streaming, GS frame presentation, HUD hooks). Any enhancement, bugfix, or compatibility modification to `third_party/PS2Recomp` is welcome and encouraged. Record every change as a tracked patch in `tools/patches/<dependency>/NNNN-<topic>.patch` (applied by `tools/runtime_patches.py`, verified by `tests/test_runtime_patches.py`) so modifications survive fresh checkouts, stay reviewable, and can feed a dedicated game-compatible fork repository. Changes to GPL code remain under the GPL.
- The PS2Recomp TOML stays minimal: no speculative `stubs`, `skip`, or instruction patches.
