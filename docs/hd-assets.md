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

## C++ Runtime Integration (`aoe2::assets`)

The static recompilation runtime directly consumes these imported assets via the platform-agnostic library `aoe2_interfaces` (modules `aoe2::assets::IAssetProvider` and `aoe2::assets::LooseHdAssetProvider`).

1. **Path Resolution**: `AssetPathResolver::ResolveRoot()` locates the assets directory dynamically:
   - Desktop: `./assets/` or `$XDG_DATA_HOME/aoe2-recomp/assets/`
   - Switch: `sdmc:/switch/aoe2/assets/`
   - Android: `assets/`
2. **Format Sniffing**: Assets are identified by initial header bytes (`2.0N` for SLP 2.0N sprites, `RIFF` for sound effects, `BINA` for UI layouts).
3. **Decoupled Strings**: Localized string tables (`key-value-strings-utf8.txt`) are loaded directly into an in-memory hash map, avoiding PS2 disc regional partitions.
4. **Architecture Specification**: Full details on decoupling and pipeline architecture can be found in `docs/architecture-hd-asset-decoupling.md` and `docs/sot/subsystems-assets-evolution.md`.

## Provenance and licences

The layout paths and the sentinel names come from the openage project's public converter configuration (`cfg/converter/games/game_editions.toml`, GPL-3.0). That is used as a fact about the game's file layout. The code here is written from the public format descriptions and does not copy openage source. The SLP header offsets come from openage's `doc/media/slp-files.md`.

## Open questions and verified findings

Analysis of the real user-owned AoE2 HD Edition v5.8 installation confirmed:
1. **Interface asset counts:** `resources/_common/drs/interface` contains 354 assets (170 SLP 2.0N, 50 RIFF WAV sound clips, 134 BINA UI/palette/table files).
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
