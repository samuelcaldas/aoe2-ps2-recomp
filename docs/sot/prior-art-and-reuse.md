# Prior Art, Reuse Sources, and Permissions (Source of Truth)

Survey date: 2026-10-20. Commits are the default-branch heads on that date; re-check before reusing anything.
No PS2 game has a native Nintendo Switch port yet; the entries below are the closest building blocks.

## 1. Reuse permissions

| Source | License | Allowed use |
| :--- | :--- | :--- |
| PS2Recomp (customized fork / runtime) | GPL-3.0 | **Full modification authorized**: runtime enhancements, bug fixes, and game-specific compatibility adaptations (IOP handshakes, overlay dispatch, CDVD streaming, GS frame presentation) are actively encouraged and tracked via patches in `tools/patches/ps2recomp/`. |
| `samuelcaldas/age-of-empires-2-decomp` (local: `../aoe`) | Owner's project, no license file | **Authorized by the owner**, including copying its code into tracked files (2026-10-24), as a reference and a source when the generated code alone is not enough: mouse and other peripherals, input polling, structure layouts, game behavior. See §4. |
| Play! PS2 Emulator (`jpd002/Play-`) | BSD 2-Clause | **Runtime Source of Truth (SOT)** for reference hardware behavior, SIF/IOP HLE contracts, boot handshakes, and overlay loading (see `docs/sot/play-emulator-sot.md`). |
| `KIexster/god-of-war-recomp` | GPL-3.0 | Code reuse allowed (GPL-compatible with PS2Recomp); game-specific overrides do not apply to AoE2. |
| `centollOS/SwitchWakerHD` | MPL-2.0 (vendored parts: see repo) | Code reuse allowed per file, keeping MPL-2.0 headers on reused files. |
| `Trinalord777/DK64Recomp-NX` | GPL-3.0 | Code reuse allowed. |
| `DLTcollab/sse2neon` | MIT | Already consumed by PS2Recomp on ARM (`v1.9.1`, `USE_SSE2NEON`). |
| `Waffle565/God-Of-War-Native-Port` | **No license** | Architecture inspiration only; do not copy code. Use the GPL-3.0 copy in `KIexster/god-of-war-recomp`, which ships this runtime with the author's permission. |
| Third-party decompilations of other games (ICO-pc, Dark Cloud, ...) | Varies | Not used. |

## 2. Projects and what to take from them

### PS2Recomp upstream: ARM is already supported
The pinned revision ships `android/` (Gradle + NDK, raylib) and `vita/` (VitaSDK, gl4es, raylib) runners.
On ARM targets its root CMake fetches sse2neon and defines `USE_SSE2NEON`; `ps2_runtime.h` then includes `sse2neon.h` in place of the x86 intrinsic headers.
**Consequence:** the ARM64 portability gate needs the probe to pass `USE_SSE2NEON` and the sse2neon include path, not a new SIMD layer and not generator changes. The Vita runner is the closest model for a console homebrew runner.

### God of War native runtime (Waffle565, via KIexster/god-of-war-recomp @ `92ece58`)
PS2Recomp-based. It replaces each PS2 hardware block with native code instead of emulating it:
- GS: native OpenGL render-target renderer, texture/CLUT store instead of a VRAM model, 2x internal resolution, 16:9.
- VU1: ~500 microprograms compiled ahead of time to C++.
- IOP: the game's IRX modules compiled ahead of time to C++ (boot time dropped from 95 s to 20 s).
Take: the AOT approach for VU1 and IRX modules, and the render-target GS design for whatever the high-level renderer cannot cover.

### SwitchWakerHD (centollOS @ `ccec6af`)
Wii U static recompilation running natively on Switch: libnx platform layer, GX2 reimplemented on deko3d, GLSL compiled on the console with uam and cached to disk, separate handheld and docked resolutions, touchscreen used as the GamePad screen, title-mode launch for memory, `devkitpro/devkita64` Docker builds, and job limits because the generated sources are memory-heavy.
Take: the Switch backend layout (platform layer, deko3d renderer, shader cache), the Docker build, and the touchscreen precedent.

### DK64Recomp-NX (Trinalord777 @ `c8d12fe`)
N64Recomp + N64ModernRuntime on Switch homebrew, with GLideN64 on OpenGL instead of the Vulkan PC renderer.
Take: an OpenGL fallback path, simpler than deko3d, if a first Switch boot is needed before a deko3d renderer.

## 3. Decision: graphics API for desktop and Switch

**Decision (2026-10-20):** OpenGL ES 3 through Mesa is the graphics API for both targets. Vulkan is not adopted; deko3d is a later, measurement-driven optimization.

Switch homebrew graphics stacks surveyed:

| Stack | Provides | Maturity |
| :--- | :--- | :--- |
| devkitPro `switch-mesa` 20.1 (nouveau nvc0) | OpenGL / OpenGL ES / EGL | Stable; used by most ports (e.g. DK64Recomp-NX via GLideN64) |
| Mesa 26 for Horizon (`danfromtico/mesa-switch` and forks) | Modern GL/GLES plus Vulkan 1.3 via NVK, statically linked into the NRO | New in 2026, split across forks; no external memory FD or DMA-BUF on Horizon |
| `gnaghi/SwitchGLES` | OpenGL ES 2/3 implemented on deko3d | Experimental |
| `nx-mod/sdl3-nx` | SDL3 with SDL_GPU on deko3d, optional NVK Vulkan | Early |
| deko3d (in `third_party_provenance.lock.json`) | Native low-level Switch GPU API, Vulkan-like | Stable; used by SwitchWakerHD |

Rationale:
- The PS2Recomp runtime already renders through raylib on OpenGL/GLES, and upstream's Vita runner proves raylib on a console. Stable Mesa on Switch should let the first Switch boot reuse that runtime before any renderer work. Unverified: whether raylib builds against devkitPro's SDL2/EGL; check this first in the `devkitpro/devkita64` image.
- AoE2 is 2D isometric sprites, tiles, and UI. GLES 3 is more than enough on the Tegra X1; the expected bottleneck is the CPU running recompiled code, not the GPU.
- One GLES 3 backend for `IRenderer` runs on desktop and Switch, so the renderer is validated on x86-64 first, in line with the target order.
- Licensing is not a blocker: Mesa is mostly MIT and the project is already GPL-3.0 through PS2Recomp.

Sequence:
1. First Switch boot: devkitPro stable Mesa + SDL2/raylib (in the `devkitpro/devkita64` Docker image), reusing the PS2Recomp runtime.
2. A shared OpenGL ES 3 `IRenderer` backend for desktop and Switch.
3. Only if profiling shows the GPU path is the bottleneck: a deko3d backend modeled on SwitchWakerHD.

**Toolchain decision (2026-10-20):** devkitPro is used only through Docker, never installed on the host. Switch builds run in the official `devkitpro/devkita64` image (libnx, switch-mesa, switch-sdl2 via `dkp-pacman`) on the `docker-dev` context, with the repository mounted into the container, as SwitchWakerHD and DK64Recomp-NX do. Pin the image by digest once the first Switch build works, and apply the same memory-heavy job limits as the desktop probe runner.

Revisit when Mesa 26/NVK consolidates upstream in devkitPro, or if profiling contradicts the CPU-bound assumption.

## 4. Using the AoE2 decompilation

The owner's matching decompilation (`samuelcaldas/age-of-empires-2-decomp` @ `d811ab3`, local `../aoe`) targets the same PAL `SLES-50282` binary. It is the owner's own work and serves as the definitive reference and code source for native replacement across all subsystems.

Rules:
- **Full code reuse authorized:** Any matched C/C++ from the decompilation (source, headers, names, addresses, structure layouts, sidecar metadata) may be copied directly into tracked files of this repository. It may replace generated code through host-side override hooks (ADR 0001) or be compiled directly for the host.
- Each such file states its origin (decomp repository and commit).
- **Still not tracked:** game binaries, ISOs, Ghidra projects/exports, function maps, disassembly produced from the binary, and generated C++.
- Do not modify the decomp repository from here. Generated C++ is never edited in place.

## 5. Sources
- https://github.com/ran-j/PS2Recomp
- https://github.com/KIexster/god-of-war-recomp
- https://github.com/Waffle565/God-Of-War-Native-Port
- https://github.com/centollOS/SwitchWakerHD
- https://github.com/Trinalord777/DK64Recomp-NX
- https://github.com/N64Recomp/N64ModernRuntime
- https://github.com/DLTcollab/sse2neon
- https://recompendium.com/en/
- https://github.com/danfromtico/mesa-switch
- https://github.com/gnaghi/SwitchGLES
- https://github.com/nx-mod/sdl3-nx
