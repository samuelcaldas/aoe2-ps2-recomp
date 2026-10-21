# Age of Empires II: The Age of Kings — PS2 Native Recompilation Port

> **Current status (2026-10-21): desktop runtime bring-up; not playable.**
> The Ghidra → PS2Recomp pipeline produces C++ that compiles and links with the pinned upstream runtime on Linux x86-64. Headless initialization succeeds, and the desktop game runner starts guest execution, but the latest diagnostic run stalls in the SIF boot handshake before input initialization or any real frame. No game binaries, assets, or generated game code are distributed here.

## 1. Objective and Current Status

Build a reproducible native static-recompilation port of **Age of Empires II: The Age of Kings**, PlayStation 2 PAL (`SLES-50282`, `AOK.ELF`), with **Nintendo Switch homebrew as the primary target**. Bring-up proceeds through desktop Linux x86-64 validation, an ARM64 portability gate, and then Switch. Android build and validation tooling also exists; its presence does not establish a working port.

### What works and what remains

| Area | Recorded status |
| :--- | :--- |
| Authoritative code generation | Ghidra export with data-region and function-map guardrails; 17,047 map entries and 17,048 generated C++ translation units in the successful desktop build. |
| Desktop compilation and linking | All generated units link with the pinned PS2Recomp runtime, without game-specific stubs, skips, or edits to generated C++. |
| Headless initialization probe | Guest memory and subsystems initialize, the owned ELF loads, the entry point resolves, and all 17,047 map entries are registered. This probe does **not** execute guest code. |
| Desktop game runner | Starts guest execution, but does **not** complete boot. The latest 60-second diagnostic baseline is `stalled_in_loop`, with zero real frames. |
| Input | Platform-neutral touch, USB mouse, and keyboard adapters are implemented and tested. KCET mouse hooks and an ELF-anchored hook resolver exist; the boot stall prevents in-game validation. Dedicated KCET keyboard hooks are not implemented. |
| Rendering | `IRenderer` and a headless `NullRenderer` exist. A shared OpenGL ES 3 backend is the chosen direction, not a completed renderer. |
| ARM64, Android, and Switch | Build infrastructure exists; successful gameplay and Switch compatibility are not established. |

### Immediate blocker

The guest waits for an IOP-side reply during the **SIF boot handshake**. The pinned runtime has no reply path for that handshake, so the guest polls indefinitely. Keyboard, controller, and memory-card services are not reached; adding input hooks does not fix this earlier boot stall. The diagnostic run records no missing branch targets and no real frames.

See [the diagnostic baseline and cause](docs/sot/kcet-stall-log.md). A faithful upstream reply path is the first option to evaluate; fabricated return values or bypass hooks are not acceptable.

A separate known gap is the ELF's runtime-loaded executable overlays: their code must be located on the owned disc and brought through the same guarded recompilation pipeline before execution can rely on them. See [the desktop probe report](docs/desktop-runtime-probe.md#71-finding-runtime-loaded-overlays).

### Architecture and non-goals

- Replace hardware-facing behavior at the **KCET boundary**, preserving the game's own input and engine logic rather than implementing a full PS2 emulator. See [ADR 0001](docs/adr/0001-kcet-boundary-replacement.md).
- Preserve native controller, mouse, and keyboard input channels. On Switch handheld, touchscreen gestures supply mouse actions; docked USB mouse and keyboard are intended to mirror the PS2 peripherals. These are adapter capabilities and design goals, not validated gameplay claims.
- Use **OpenGL ES 3 through Mesa** as the shared desktop/Switch graphics direction. Vulkan is not adopted; deko3d is reserved for a later profiling-driven decision. See [graphics and toolchain decisions](docs/sot/prior-art-and-reuse.md#3-decision-graphics-api-for-desktop-and-switch).
- No distribution of copyrighted game code or assets, no tracked generated C++, no full GS/IOP software emulation, and no gameplay alterations during bring-up.

## 2. Legal and Provenance Rules

- **User-owned inputs:** provide a legally owned PAL executable and disc locally. Game binaries, disc images, extracted data, Ghidra projects, function maps, and generated C++ remain git-ignored.
- **Authoritative pipeline:** function maps must come from Ghidra. The local fallback analyzer is not accepted by the generation-manifest verifier. Data-spanning entries must be fixed during Ghidra analysis, not hidden with skips or generated-code edits.
- **No fabricated results:** do not add speculative stubs, skips, instruction patches, or placeholder return values to make boot appear successful.
- **Reference exception:** unofficial disassembly material is not imported by default. The owner explicitly authorizes their matching `samuelcaldas/age-of-empires-2-decomp` repository (`../aoe`) as a reference for peripheral hooks and engine behavior. Do not modify it from this repository. Game function names, addresses, disassembly, and binary-derived code snippets must stay out of tracked files here. See [the reuse rules](docs/sot/prior-art-and-reuse.md#4-using-the-aoe2-decompilation).
- **Pinned dependencies:** `third_party_provenance.lock.json` records dependency revisions and licenses; source trees live in ignored `third_party/` and are managed by `tools/bootstrap_dependencies.py`.
- **Licensing:** PS2Recomp is **GPL-3.0**, and runtime code linking it inherits GPL obligations. libnx is **ISC**; deko3d is **Zlib**. Check [prior art and reuse permissions](docs/sot/prior-art-and-reuse.md) before copying third-party code.

## 3. Prerequisites

For the lightweight host scaffolding and tests:

- Linux x86-64, a C++20 compiler, CMake >= 3.20, Ninja, Python >= 3.10, and Git.

For code generation and full runtime bring-up:

- Ghidra with `GHIDRA_HOME` configured, a compatible Java runtime, and the pinned Emotion Engine/R5900 language extension installed by the project tool.
- The pinned PS2Recomp dependencies and verified local game inputs.
- Substantial disk space and memory for thousands of generated translation units. Use the resource-limited build wrappers below rather than an unrestricted full build.
- Docker with the **`docker-dev` context** for containerized desktop, Android, and Switch builds. **devkitPro runs only inside Docker**, never on the host.
- For a native desktop probe build: systemd user scopes and the runtime's raylib, FFmpeg, and OpenGL/GLX dependencies. See [the probe report](docs/desktop-runtime-probe.md).
- For desktop execution validation: `Xvfb`, `xwd`, and ImageMagick (`convert` and `magick`), plus the diagnostic game binary and owned ELF.

## 4. Repository Layout

```text
aoe2-ps2-recomp/
├── CMakeLists.txt                   # Interface tests and opt-in runtime/game targets
├── cmake/                          # Generated-code and upstream runtime integration
├── third_party_provenance.lock.json # Pinned dependency revisions and licenses
├── generation_manifest.json        # Sanitized generation provenance
├── core/                           # Shared target metadata and constants
├── runtime/                        # Guest memory and platform-service interfaces
├── platform/                       # Desktop, Android, and Switch runners/integration
├── input/                          # Touch, USB mouse/keyboard, guest adapters and RPC model
├── game_hooks/                     # Host-side KCET mouse replacements
├── renderer/                       # Rendering interface and NullRenderer
├── docker/                         # Desktop, Android, and Switch build images
├── tools/                          # Import, generation, build, validation and audit tools
├── docs/                           # Architecture, reports, ADRs and reuse rules
└── tests/                          # Python and C++ tests
```

Local inputs and derived artifacts live in ignored `game/`, `analysis/`, `generated/`, `third_party/`, and `build/` paths. Validation screenshots belong only in ignored `.playwright-mcp/`.

## 5. Setup and Validation Commands

Run commands from the repository root. The lightweight tests do not require the owned game or generated code; full generation and runtime builds do.

### Lightweight host tests

```bash
python3 -m unittest discover -s tests
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The default CMake build tests the host interfaces; it does not compile or boot the game.

### Dependencies and owned inputs

```bash
python3 tools/bootstrap_dependencies.py
python3 tools/bootstrap_dependencies.py --check
python3 tools/import_owned_game.py --source /path/to/AOK.ELF
python3 tools/verify_owned_game.py
python3 tools/import_owned_disc.py --iso /path/to/aoe2.iso
```

The expected executable is `game/SLES-50282/AOK.ELF`, SHA-1 `f478e19f2059d52899b691483ef5c7522e30e17c`. Disc extraction places local files beside it; those files are also needed to investigate runtime-loaded overlays.

### Ghidra export and guarded code generation

With `GHIDRA_HOME` set and dependencies available:

```bash
python3 tools/install_ghidra_ee_extension.py
python3 tools/validate_ghidra_env.py
python3 tools/run_ps2recomp_pipeline.py
python3 tools/verify_function_map.py
python3 tools/verify_generation_manifest.py
```

The pipeline runs Ghidra analysis and export, validates function boundaries, generates C++, and records provenance. It stops if Ghidra is missing or the map violates guardrails. See [the detailed workflow](docs/ghidra-ps2recomp-workflow.md).

### Headless desktop initialization probe

Build the native probe only through its memory-capped wrapper; adjust the job count to available resources:

```bash
python3 tools/run_desktop_probe_build.py --jobs 3
ctest --test-dir build/desktop-probe -R desktop_runtime_probe_init --output-on-failure
```

This checks initialization, ELF loading, and registration, **not guest execution or gameplay**.

### Desktop game runner and diagnostic validation

After generation, resolve local mouse hooks from the owned ELF and function map, then build the diagnostic runner in Docker:

```bash
python3 tools/resolve_kcet_hooks.py
python3 tools/docker_build.py --platform desktop --diagnostics --jobs 3
python3 tools/validate_desktop.py --run-seconds 60 --interval 15
```

The resolver writes ignored `game/SLES-50282/kcet_hooks.ini`; `--decomp-root ../aoe` optionally cross-checks the result against the authorized reference. Hooks do not resolve the current SIF boot blocker.

Validation runs `build/docker-desktop-diagnostics/aoe2_game`, saves its log under `build/desktop-validation/`, and captures screenshots in `.playwright-mcp/`. A pass requires guest progress and at least two changing real frames, not merely a live process or the runtime's fallback image. **The latest recorded baseline fails these criteria** with `stalled_in_loop` and zero real frames.

Container build entry points also exist for normal desktop, Android, and Switch builds:

```bash
python3 tools/docker_build.py --platform desktop --jobs 3
python3 tools/docker_build.py --platform android --jobs 3
python3 tools/docker_build.py --platform switch --jobs 3
```

These commands describe the available tooling, not verified boot or gameplay on those targets. Switch builds use the devkitPro Docker image through `docker-dev`.

### EE game TTY diagnostics

The project includes a minimal DECI2 host for the game's EE debug output. It captures packets supplied by the guest handler through `ExSend` and delivers write-completion callbacks through the EE scheduler. Captured text is identified by `[game-tty]` on stderr, separately from runtime instrumentation and IOP logs. This does not bypass asserts or make the game playable.

The CMake option `AOE2_GAME_TTY` defaults to `ON`, independently of debug tracing; `OFF` reports that no host is attached. Synthetic transport tests are part of the default host build. Enable `AOE2_BUILD_DECI2_RUNTIME_TESTS` for isolated scheduler and lifecycle tests without generated game code. See [EE game TTY](docs/game-tty.md) for the protocol, build integration, and validation limits.

## 6. Technical Documentation

- [Architecture](docs/architecture.md) — layer boundaries, platform strategy, and input design.
- [Ghidra and PS2Recomp workflow](docs/ghidra-ps2recomp-workflow.md) — export, guardrails, generation, and manifest verification.
- [Desktop runtime probe](docs/desktop-runtime-probe.md) — historical compile/link diagnostics, successful full build, headless initialization, and overlay finding.
- [Current desktop boot stall](docs/sot/kcet-stall-log.md) — diagnostic baseline and SIF handshake cause; supersedes the probe report's earlier non-execution status for the separate game runner.
- [KCET boundary replacement](docs/adr/0001-kcet-boundary-replacement.md) — accepted replacement strategy and input-hook contracts.
- [Prior art and reuse](docs/sot/prior-art-and-reuse.md) — permissions, licenses, graphics decision, and Docker-only Switch toolchain policy.
- [Runtime linkage audit](docs/runtime-linkage-audit.md) — runtime integration analysis.
- [Optional HD asset tooling](docs/hd-assets.md) — local asset import and inventory, separate from the original PS2 boot milestone.
