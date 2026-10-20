#!/usr/bin/env python3
"""Write a local, untracked inventory of the imported HD-edition assets.

The report stays in the ignored game folder. It holds counts, id ranges, and SLP sizes,
never file names, so it can be read while answering open questions about the interface.
"""
import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.hd_assets.errors import HdAssetError
from tools.hd_assets.inventory import render_inventory
from tools.hd_assets.manifests import read_catalog

CATALOG_NAME = "catalog.json"
INVENTORY_NAME = "inventory.txt"


def parse_args():
    parser = argparse.ArgumentParser(description="Inventory imported HD-edition assets.")
    parser.add_argument(
        "--dest-dir",
        type=Path,
        default=REPO_ROOT / "game" / "hd-edition",
        help="Destination directory used by import_hd_assets.py",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    dest_dir = args.dest_dir.resolve()
    catalog_path = dest_dir / CATALOG_NAME
    if not catalog_path.is_file():
        sys.stderr.write(f"Error: catalog not found, run import_hd_assets.py first: {catalog_path}\n")
        sys.exit(1)
    try:
        text = render_inventory(read_catalog(catalog_path), dest_dir)
    except HdAssetError as error:
        sys.stderr.write(f"Error: {error}\n")
        sys.exit(1)
    (dest_dir / INVENTORY_NAME).write_text(text, encoding="utf-8")
    print(f"Wrote {dest_dir / INVENTORY_NAME}")


if __name__ == "__main__":
    main()
