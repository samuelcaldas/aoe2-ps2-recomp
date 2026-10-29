# Implementation Plan: Native Expanded Viewport Implementation

## Context

The objective is to implement native map viewport expansion for *Age of Empires II: The Age of Kings* (PS2 PAL `SLES-50282`). Rather than naively scaling or letterboxing the original 640×262 playfield, the game engine will software-rasterize a larger visible area of terrain and world objects at a 1:1 pixel scale (1280×534 for 720p handheld Switch and 1920×708 for 1080p desktop/docked Switch) while anchoring legacy HUD bands (resource bar top, command bar bottom) to screen borders.

This plan continues the work already started across the dedicated fork `third_party/PS2Recomp` and the main repository on branch `native-map-reservation-2026-10-29`, strictly respecting all safety and validation gates.

---

## Architectural Context & Gate Sequence

The implementation is governed by seven strict gates:

1. **Gate 1: Contract Characterization & Observation Baseline** (PASSED)
   - Verified via `tests/cpp/map_area_contract_tests.cpp`.
   - Identified panel dimensions at `+0x14`/`+0x18`, 5 top and bottom guard rows, and the 917 row table capacity limit (`+0xD4` to `+0xF27`).
   - Verified palette copy `0x001AE350` reading 256 RGBA entries from `*a0 + 0x5C`.

2. **Gate 2: 8 MiB Exclusive Guest Arena** (PASSED)
   - Pinned in `third_party/PS2Recomp` (`e454f34`, `f519a6b`) reserving `0x036C0000`–`0x03EC0000` below HLE with `claimMapArena` / `releaseMapArena` / `validateMapArena`.

3. **Gate 3: Legacy Relocation Parity Gate** (READY FOR VALIDATION)
   - Relocate the legacy 640×262 surface to the arena and verify byte-for-byte visual and gameplay parity before expanding dimensions.

4. **Gate 4: Coherent Surface Sizing & Enlargement**
   - Size surfaces coherently (1280×534 for 720p; 1920×708 for 1080p).
   - Ensure 16-byte aligned pitch, 5 guard rows, and height $\le$ 917 to protect `+0xF28`.

5. **Gate 5: Host Frame Composition Seam**
   - Integrate `MapViewCompositor` with `PS2Runtime::setFrameCompositor` before graphics upload and swap.
   - Fall back to legacy frames during non-gameplay states (menus, loading screens).

6. **Gate 6: Input Routing Consistency**
   - Integrate `MapInputRouter` into `kcet_mouse_overrides.cpp` to correctly dispatch viewport clicks to local world coordinates and border clicks to the legacy HUD.

7. **Gate 7: Full Mission Validation & Platform Portability**
   - Validate tutorial mission replay and compile on Switch (`docker-dev`).

---

## Implementation Stages

### Stage 1: Legacy Relocation Parity Verification (Gate 3)
- Run `AOE2_MAP_VIEW=relocation` to relocate the 640×262 surface while retaining legacy presentation and input.
- Inspect canaries and verify visual parity against baseline screenshot.
- This mode is available for investigation; parity has not yet been established. Native enlargement must not become the default until the remaining gates pass.

### Stage 2: Sizing & Hook Dynamic Dimensions (Gate 4)
- Update `map_view_module.cpp` to support `Phase::NativeEnlargement`.
- Configure width and height dynamically based on `MapViewLayout::ForOutput`.

### Stage 3: Presentation Compositor Wiring (Gate 5)
- Wire `PS2Runtime::setFrameCompositor` in `MapViewSession` to consume captured map snapshots.
- Bypasses the 1024×1024 GS texture ceiling and renders 1:1 map pixels with anchored HUD bands.

### Stage 4: Input Router Integration (Gate 6)
- Connect `MapInputRouter` to `kcet_mouse_overrides.cpp`.
- Enable full world navigation and precise HUD interaction.

### Stage 5: Verification & Deliverables (Gate 7)
- Run CTest and Python test suites.
- Verify 720p and 1080p campaign mission captures.
- Ensure Docker Switch compatibility.
