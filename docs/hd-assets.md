# HD edition assets: local import and catalog

Phase 1 of the optional HD asset source described in `architecture.md` §7.3. The tools copy a user-owned Age of Empires II HD Edition install into the ignored `game/` folder, verify every copy, and record only digests in the tracked tree. Nothing from the game is committed.

## Commands

```bash
# Import selected categories from an installed HD edition folder (default category: interface)
python3 tools/import_hd_assets.py --source "<HD install folder>" --categories interface,graphics

# Verify copies, local catalog, and the tracked summary; exits 1 and lists each problem
python3 tools/verify_hd_assets.py

# Write game/hd-edition/inventory.txt with counts, id ranges, and SLP sizes (local only)
python3 tools/inventory_hd_assets.py

# Unit tests; the real-install test runs only when AOE2_HD_SOURCE points at an install
python3 -m unittest tests.test_hd_assets
AOE2_HD_SOURCE="<HD install folder>" python3 -m unittest tests.test_hd_assets.RealInstallTest
```

Outputs:

| Path | Tracked | Content |
|---|---|---|
| `game/hd-edition/source/<install-relative path>` | no | byte copies of the selected files |
| `game/hd-edition/catalog.json` | no | one row per file: category, path, numeric id, format, size, SHA-256 |
| `game/hd-edition/inventory.txt` | no | counts and SLP sizes for the open questions in `architecture.md` §7.5 |
| `hd_assets_manifest.json` | yes | edition key, tool version, counts, and one SHA-256 per category |

The tracked summary holds no file names, ids, or paths. `verify_hd_assets.py` rejects any extra key and any value containing a path separator.

Each import merges into `catalog.json`, so importing `interface` now and `graphics` later leaves both listed. Re-importing an unchanged file is a no-op. A destination that differs from its source is refused, never overwritten.

## Layout and profile

The profile is declared in `core/hd_source.py` (`HD_2013`). Its sentinel files are `AoK HD.exe` and `resources/_common/dat/empires2_x1_p1.dat`. Categories:

| Category | Source path |
|---|---|
| `interface` | `resources/_common/drs/interface/` |
| `graphics` | `resources/_common/drs/graphics/` |
| `terrain` | `resources/_common/terrain/textures/` |
| `gamedata` | `resources/_common/drs/gamedata_x1/` |
| `datfile` | `resources/_common/dat/empires2_x1_p1.dat` |

The HD edition does not ship `.drs` containers. Each asset is a loose file named by its numeric id. The format is read from the first bytes (`format_sniffer.py`), never from the extension. The 2013 edition uses SLP `2.0N`; the Definitive Edition uses SLD, which is a separate profile for later.

Palettes are not a separate category: the HD profile keeps them in the `interface` folder, so `interface` includes them.

## Asset provider and build staging

The codebase defines `IAssetProvider`, `LooseHdAssetProvider`, and `AssetPathResolver` in `assets/`, but the game runtime does not initialize or call the provider yet. The workflow below prepares the provider's expected loose files; it does not make HD sprites visible in-game. SLP decoding, palette application, PS2-to-HD correlation, and runtime wiring remain future work owned by the runtime integration hand-off.

```bash
# Stage the imported catalog for one target (the Docker build driver does this automatically when catalog.json exists)
python3 tools/stage_hd_assets.py --platform desktop --dry-run
python3 tools/stage_hd_assets.py --platform desktop
python3 tools/verify_hd_stage.py --platform desktop
```

| Build target | Prepared `assets/` directory | Delivery |
|---|---|---|
| Desktop | `game/SLES-50282/assets/` | Beside `AOK.ELF`; initialize the provider with this explicit path when runtime wiring is added |
| Android | `game/SLES-50282/assets/` | `validate_android.py` pushes the PS2 data directory, including `assets/`, to app external files |
| Switch | `build/hd-stage/switch/assets/` | Copy this directory to `sdmc:/switch/aoe2/assets/` beside the NRO; no automatic SD-card deployment exists |

Only files in the local catalog are staged. Original filenames, extensions, and bytes are preserved under the provider categories. The local `hd_stage.json` stores per-file digests under ignored game/build output. Desktop and Android share the same staged source tree and portable manifest. The Switch build outputs its own copy for manual packaging.

`AssetPathResolver::ResolveRoot()` defaults to Desktop `./assets/` or `$XDG_DATA_HOME/aoe2-recomp/assets/`, Switch `sdmc:/switch/aoe2/assets/`, and Android relative `assets/`. The staging directories above are not automatically passed to that resolver because runtime initialization is not wired yet; the runtime hand-off must choose explicit roots consistent with this table.

The current `LooseHdAssetProvider` searches loose files by numeric ID and extension (`.slp`, `.bina`, `.wav`, `.mp3`, `.dat`, `.txt`). Its format sniffer recognizes SLP, RIFF, and BINA signatures. Content inspection of HD v5.8 found JASC-PAL palettes stored as `.bina` files and BMP images; staging preserves these originals. A standalone strict JASC-PAL parser validates the palette bytes, but no runtime decoder is present. See `docs/architecture-hd-asset-decoupling.md` for the broader target architecture.

## Provenance and licences

The layout paths and the sentinel names come from the openage project's public converter configuration (`cfg/converter/games/game_editions.toml`, GPL-3.0). That is used as a fact about the game's file layout. The code here is written from the public format descriptions and does not copy openage source. The SLP header offsets come from openage's `doc/media/slp-files.md`.

## Open questions and verified findings

Analysis of the real user-owned AoE2 HD Edition v5.8 installation confirmed:
1. **Interface asset counts:** `resources/_common/drs/interface` contains 354 assets: 170 SLP 2.0N, 50 RIFF WAV sound clips, 32 JASC-PAL palettes, 8 BMP images, and 94 other BINA/UI/table files. The 32 palettes each declare 256 RGB colors.
2. **Resolution-specific HUD bases:** The HD set provides multi-resolution in-game UI layouts in SLP:
   - 800x600 (`50100` and variants)
   - 1024x768 (`50101`, `51121`)
   - 1280x1024 (`50102`, `51141`)
3. **Modular filler bars:** HD supplies modular bar SLPs used to span arbitrary screen widths without stretching panels:
   - Top resource / border bar (`50792`, 697x24)
   - Bottom command / status bar elements (`50150`, 592x90)
   - Vertical side panels (`50341`, 150x768)
4. **Modernized menus:** Out-of-game menus (main menu, campaign menu) in the HD edition are defined via JSON layout descriptors (`resources/_common/textures/ui/main-menu/default/main-menu.json`) with RGBA PNG button states and JPEG background artwork.
5. **Widescreen HUD viability:** For widescreen rendering on Nintendo Switch (1280x720) or desktop 1080p, `architecture.md` §7.4 Option 1 (re-laid-out HUD anchoring panels to screen edges and filling gaps with modular bar graphics) is fully supported by the available HD asset set.
