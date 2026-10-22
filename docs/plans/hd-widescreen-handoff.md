# Hand-off: HD asset staging and widescreen HUD runtime integration

This note is for `@aoe-recomp`. It defines the consumer boundary for the HD asset staging and HUD geometry implementation without modifying runtime/KCET code in the active workspace.

## Existing provider contract

- `aoe2::assets::IAssetProvider` is in `assets/include/aoe2/assets/asset_provider.hpp`; `LooseHdAssetProvider` is in the adjacent header/source.
- `AssetPathResolver` is `assets/include/aoe2/assets/asset_path_resolver.hpp` / `assets/src/asset_path_resolver.cpp`.
- The provider loads numeric IDs and searches loose `.slp`, `.bina`, `.wav`, `.mp3`, `.dat`, `.txt` files under `assets/<category>/`.
- The resolver defaults are Desktop `./assets` or XDG, Android relative `assets`, Switch `sdmc:/switch/aoe2/assets`. Runtime initialization does not currently call the provider. When it does, pass an explicit path to match the staged location below rather than assume the process working directory.

## Staged outputs

`tools/docker_build.py` now prepends an optional stage step to desktop, Android, and Switch build commands. It runs only if `game/hd-edition/catalog.json` exists; PS2-only builds continue when HD assets have not been imported.

| Target | Staging root | Provider root passed by the runtime | Delivery |
|---|---|---|---|
| Desktop | `game/SLES-50282/assets/` | `<ELF directory>/assets` | Present beside the owned data root |
| Android | `game/SLES-50282/assets/` | `<app external files dir>/assets` | Existing Android validation `adb push` copies the full PS2 data directory |
| Switch | `build/hd-stage/switch/assets/` | `sdmc:/switch/aoe2/assets` | Copy generated assets beside the NRO on the SD card; no automated SD deployment |

The stager copies original bytes under provider categories. HD v5.8's current imported catalog has only `interface` (354 files). To stage more categories, import them first. Desktop and Android intentionally share a portable asset tree and manifest in the same ignored data root. The Switch build uses a separate ignored build output.

Commands:

```bash
python3 tools/stage_hd_assets.py --platform desktop --dry-run
python3 tools/stage_hd_assets.py --platform desktop
python3 tools/verify_hd_stage.py --platform desktop
python3 tools/stage_hd_assets.py --platform switch
python3 tools/verify_hd_stage.py --platform switch
```

## Real HD v5.8 format findings

`resources/_common/drs/interface` has 354 loose files: 170 SLP 2.0N, 50 RIFF WAV, 32 `JASC-PAL` palettes stored with `.bina` extensions, 8 BMP images, and 94 other BINA/UI files. Each palette has 256 RGB entries. The standalone `read_jasc_palette()` parser is a strict validator; it is not a runtime palette service.

The current C++ provider sniffing recognizes SLP, RIFF, and `BINA` signatures but not `JASC-PAL` or BMP. Asset staging preserves all files; the runtime should recognize actual content signatures before decoding. The default HD palette described by openage is asset ID 50500. The current HD interface set includes multiple resolution-specific HUD bases, but their rectangles have not been mapped to PS2 guest draw operations.

## Runtime tasks not included here

1. Initialize `LooseHdAssetProvider` with the platform root above and ensure the asset-resolution path is reached from game boot.
2. Add `JASC-PAL` and BMP format recognition to the provider and a runtime SLP 2.0N decoder/palette path. Staged `.slp` files are intentionally raw; no PNG conversion occurs in the build stage.
3. Resolve a verified PS2 guest-asset → HD asset-ID table. Preserve the PS2 path as fallback for unmatched/unsupported IDs.
4. **Do not assume `kt_gpu` is the main draw seam.** Current investigation suggests it handles splash/fade operations; verify the actual gameplay draw path before choosing a hook. No renderer hook or main draw seam is established yet. No game-specific panel coordinates are inferred by the generic HUD library.
5. Register `renderer/src/hud_layout.cpp` and `input/src/hud_inverse_mapper.cpp` in `aoe2_interfaces` and register `tests/cpp/hud_layout_tests.cpp` in CMake. These three edits were kept out of this branch to avoid editing the other agent's active CMake tree.
6. At input dispatch, call `HudInverseMapper::ToGuest(displayX, displayY)` first. If it returns no region, pass the point through the existing `SurfaceMapping` playfield path. Do not forward relocated HUD coordinates as world clicks.
7. Establish the actual guest and presenter extents before enabling widescreen. Existing docs disagree between a 640x480 guest and a 640x448 runtime window; resolve that before selecting panel source rectangles or applying playfield transforms.

## New C++ API

- `renderer::HudLayout::ForExtent(displayExtent, regionSpecs)` keeps guest coordinates at 640x480 by default; the overload accepts the guest extent explicitly.
- Each `HudRegionSpec` declares a source guest rectangle, an anchor, destination size, and safe margins. The implementation rejects non-finite geometry, validates source/destination bounds, and rejects overlapping destination regions.
- `input::HudInverseMapper` consumes the layout's resolved regions. It maps points in those relocated regions back to the original guest rectangles and returns `std::nullopt` for points outside them.
- These are geometry primitives only. This branch does not declare any actual game HUD rectangles, tie regions to SLP IDs, alter viewport/culling constants, or make the HUD visible in-game.

## Verification run

- The JASC-PAL parser was tested against all 32 real HD v5.8 palette files.
- The staging tool has synthetic tests for each platform, same-tree desktop/Android behavior, dry-run, duplicate paths, traversal rejection, source symlink containment, conflict preservation, idempotence, digest validation, missing copies, unlisted files, and manifest traversal.
- HUD geometry tests are standalone host C++ tests. They were compiled with `g++` pending registration in CMake.
