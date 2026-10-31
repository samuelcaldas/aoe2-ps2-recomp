# Implementation Plan: Native Expanded Viewport Implementation

## Context

The objective is to implement native map viewport expansion for *Age of Empires II: The Age of Kings* (PS2 PAL `SLES-50282`). Rather than naively scaling or letterboxing the original 640×262 playfield, the game engine will software-rasterize a larger visible area of terrain and world objects at a 1:1 pixel scale (1280×534 for 720p handheld Switch and 1920×708 for 1080p desktop/docked Switch) while anchoring legacy HUD bands (resource bar top, command bar bottom) to screen borders.

This plan continues the work already started across the dedicated fork `third_party/PS2Recomp` and the main repository on branch `native-map-reservation-2026-10-29`, strictly respecting all safety and validation gates.

## Current Checkpoint (2026-10-30)

- Desktop Debug diagnostics and Switch Debug builds both completed after the viewport descriptor fix; the Switch NRO was regenerated.
- A 1080p campaign run reached the map and logged ten indexed-map submissions. The live watchpoint traced the prior canary overwrite to generated `entry_0021b1c0`, the byte-fill loop. The KCET descriptor contract is `height` at `+0x38` and `width` at `+0x3c`; the native rebind had written those dimensions in the opposite order, causing a 1920-row fill. The hook now writes and validates `708` at `+0x38` and `1920` at `+0x3c`. Subsequent submissions passed the arena canaries without the previous overrun.
- The 1920×1080 composed screenshot has the expected HUD bands, but its map area remains black. Captured 1920×708 frames are dominated by palette index 255 (`RGBA 255,255,255,0`, transparent) and index 0 (`RGBA 0,0,0,128`, semi-transparent black); the few non-background colors seen on the first submission disappear after the camera is moved. This does not yet establish whether the remaining black view is fog/camera state or a rendering-path issue. The viewport bounds and memory safety are fixed; terrain output and expanded-view input parity remain unverified.
- The compositor previously treated GS palette alpha `0x80` as host alpha 128 (half-transparent). The pinned GS CPU presenter converts GS alpha to host RGBA with `min(255, alpha*2)` and normalizes presented alpha to 255. `MapViewCompositor` now applies the same conversion and emits opaque host alpha for fully opaque map colors; this correction still needs a live 1080p visual check.
- Desktop Diagnostics and Switch Debug builds pass with this alpha fix. The follow-up 1080p campaign capture reached a stable HUD and confirmed ten 1920×708 submissions with no canary or composition errors. The stable frame is still black; the minimap's player marker and viewport indicator are far apart, and later submissions contain only black index 0 and transparent index 255. A capture with the camera over revealed terrain is still required to distinguish camera/fog state from a remaining draw-path defect.
- A follow-up 1080p run confirmed the map remains black after mission start. Clicking the minimap's player marker did not visibly recenter the viewport; the click opened the game's “Shortcut Menu” hint. The diagnostic runtime then stopped on a missing guest call target reached from `SMENU_CD.BIN`, before the relocation result could be assessed. This is a generated-function coverage issue, not a canary or compositor failure. Regenerating the function map requires Ghidra, but `GHIDRA_HOME` and `analyzeHeadless` are currently unavailable. No generated C++ or stubs were added.
- The optional Switch `map_view.ini` mode parser remains opt-in. Compilation does not establish runtime rendering or input parity.
- Gate 3 relocation parity and Gate 7 in-mission visual/input validation remain open. Keep native enlargement opt-in until a non-black live map and correct input routing are demonstrated.

On Switch, place an optional `map_view.ini` beside `AOK.ELF` with one setting such as `mode=relocation` or `mode=native`. `AOE2_MAP_VIEW` remains the higher-priority override on hosts that provide process environment variables. No file means legacy rendering.

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
