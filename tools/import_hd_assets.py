#!/usr/bin/env python3
"""Import local HD-edition assets for the requested categories.

Copies only the chosen categories into the ignored game folder, verifies each copy,
merges them into the local catalog, and rewrites the tracked digest-only summary.
"""
import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.hd_source import HD_2013
from tools.hd_assets.asset_catalog import AssetCatalog
from tools.hd_assets.errors import HdAssetError
from tools.hd_assets.importer import import_assets
from tools.hd_assets.manifests import (
    build_tracked_summary,
    read_catalog,
    write_catalog,
    write_tracked,
)

CATALOG_NAME = "catalog.json"


def parse_args():
    parser = argparse.ArgumentParser(description="Import local HD-edition assets.")
    parser.add_argument(
        "--source",
        type=Path,
        required=True,
        help="Path to the installed HD edition folder",
    )
    parser.add_argument(
        "--categories",
        default="interface",
        help="Comma-separated categories: interface, graphics, terrain, gamedata, datfile",
    )
    parser.add_argument(
        "--dest-dir",
        type=Path,
        default=REPO_ROOT / "game" / "hd-edition",
        help="Destination directory (default: <repo_root>/game/hd-edition)",
    )
    parser.add_argument(
        "--tracked-manifest",
        type=Path,
        default=REPO_ROOT / "hd_assets_manifest.json",
        help="Tracked digest-only summary to rewrite (default: <repo_root>/hd_assets_manifest.json)",
    )
    return parser.parse_args()


def load_existing_catalog(catalog_path: Path) -> AssetCatalog:
    if not catalog_path.is_file():
        return AssetCatalog(())
    return read_catalog(catalog_path)


def main():
    args = parse_args()
    categories = tuple(name for name in args.categories.split(",") if name)
    dest_dir = args.dest_dir.resolve()
    catalog_path = dest_dir / CATALOG_NAME
    try:
        imported = import_assets(args.source.resolve(), HD_2013, categories, dest_dir)
    except HdAssetError as error:
        sys.stderr.write(f"Error: {error}\n")
        sys.exit(1)
    catalog = load_existing_catalog(catalog_path).merged_with(imported)
    write_catalog(catalog, catalog_path)
    write_tracked(build_tracked_summary(HD_2013, catalog), args.tracked_manifest)
    print(f"Imported {imported.total()} assets; catalog now lists {catalog.total()} in {dest_dir}")


if __name__ == "__main__":
    main()
