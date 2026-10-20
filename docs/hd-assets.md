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

## Provenance and licences

The layout paths and the sentinel names come from the openage project's public converter configuration (`cfg/converter/games/game_editions.toml`, GPL-3.0). That is used as a fact about the game's file layout. The code here is written from the public format descriptions and does not copy openage source. The SLP header offsets come from openage's `doc/media/slp-files.md`.

## Open questions

Run `inventory_hd_assets.py` after importing `interface`. Its SLP size list answers `architecture.md` §7.5 directly: if the interface has more than one size per category, the HD set ships resolution-specific UI; if it has one size, widescreen filler must come from elsewhere.
