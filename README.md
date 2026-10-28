# Age of Empires II: The Age of Kings — PS2 Native Recompilation Port

> **Current status (2026-10-21): desktop runtime bring-up; not playable.**
> The Ghidra → PS2Recomp pipeline produces C++ that compiles and links on Linux x86-64. The local PS2Recomp fork now completes the SIF boot handshake and has displayed the game's loading screen, but boot and gameplay remain unvalidated. Mouse and keyboard hooks apply successfully; this is not proof of working in-game input. These runtime changes are local and are not reproduced by the pinned dependency alone. No game binaries, assets, or generated game code are distributed here.

## 1. Objective and Current Status

Build a reproducible native static-recompilation port of **Age of Empires II: The Age of Kings**, PlayStation 2 PAL (`SLES-50282`, `AOK.ELF`), with **Nintendo Switch homebrew as the primary target**. Bring-up proceeds through desktop Linux x86-64 validation, an ARM64 portability gate, and then Switch. Android build and validation tooling also exists; its presence does not establish a working port.

### What works and what remains

| Area | Recorded status |
| :--- | :--- |
| Authoritative code generation | The current Ghidra export has 58,236 map entries and 58,237 generated C++ units. The generation manifest verifies, and the function-map guard reports zero violations. |
| Desktop compilation and linking | All generated units link with the pinned PS2Recomp runtime, without game-specific stubs, skips, or edits to generated C++. |
| Headless initialization probe | The earlier pinned-runtime probe initialized memory and subsystems, loaded the owned ELF, and resolved the entry point with its then-current 17,047-entry map. It does **not** execute guest code and is separate from the local-fork game runs below. |
| Desktop game runner | The local fork completes the SIF handshake and displays the game's loading screen. In a 300-second experiment with function-trace output discarded, all five audio-loading waits clear and the screen becomes black with a cursor. No menu or completed boot is verified; visual-progress validation still fails. |
| Input | Platform-neutral touch, USB mouse, and keyboard adapters are tested. The strict KCET registry applied all ten configured mouse/keyboard hooks in a desktop run, with no pad aliases configured. In-game input and exact keyboard event-mask semantics remain unvalidated. |
| Rendering | Native HD/GLES3 output is incomplete. The desktop uses the guest framebuffer fallback; an unverified HD overlay that obscured the HUD has been removed. See [HD asset strategy & presentation](docs/sot/hd-assets-strategy.md). |
| ARM64, Android, and Switch | Build infrastructure exists; successful gameplay and Switch compatibility are not established. |

### Immediate blocker

The local fork has **not reached a verified menu**. Startup polls an audio-side service with long bounded delays. Writing the diagnostic function trace severely limits observed progress: a 300-second file-writing run reaches the third audio-loading stage, whereas discarding only that trace output lets all five waits clear within the same window. The latter run ends on a black screen with a cursor, with 23 successful SIF disc reads and no missing branch targets. The remaining boot/rendering cause is not established; audio completion must not be forced or assumed broken.

A 180-second run confirmed all ten mouse/keyboard hooks were applied, but its validator raised a process-shutdown timeout before producing a final report. Bounded termination escalation now prevents that timeout from discarding the result. A fresh 300-second run completes reporting but still fails validation: four real frames remain unchanged, with no missing targets. See [the diagnostic history and evidence](docs/sot/kcet-stall-log.md). Fabricated status replies, semaphore signals, and bypass hooks are not acceptable.

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
├── game_hooks/                     # Strict KCET registry and mouse/keyboard replacements
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
- [HD asset tooling and strategy](docs/sot/hd-assets-strategy.md) — local asset import, inventory, and presentation pipeline.
- [Multi-platform build artifact isolation](docs/multiplatform-build-isolation.md) — build directory partitioning across Desktop, Switch, and Android for zero-collision incremental target switching.
