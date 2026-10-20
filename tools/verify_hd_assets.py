#!/usr/bin/env python3
"""Verify the local HD-edition copies, the local catalog, and the tracked digest-only summary.

Exits non-zero and lists every problem found when any check fails.
"""
import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.hd_source import HD_2013
from tools.hd_assets.manifests import read_catalog, read_tracked
from tools.hd_assets.verifier import find_copy_problems, find_summary_problems

CATALOG_NAME = "catalog.json"


def parse_args():
    parser = argparse.ArgumentParser(description="Verify local HD-edition assets.")
    parser.add_argument(
        "--dest-dir",
        type=Path,
        default=REPO_ROOT / "game" / "hd-edition",
        help="Destination directory used by import_hd_assets.py",
    )
    parser.add_argument(
        "--tracked-manifest",
        type=Path,
        default=REPO_ROOT / "hd_assets_manifest.json",
        help="Tracked digest-only summary to check",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    dest_dir = args.dest_dir.resolve()
    catalog_path = dest_dir / CATALOG_NAME
    if not catalog_path.is_file():
        sys.stderr.write(f"Error: catalog not found, run import_hd_assets.py first: {catalog_path}\n")
        sys.exit(1)
    if not args.tracked_manifest.is_file():
        sys.stderr.write(f"Error: tracked manifest not found: {args.tracked_manifest}\n")
        sys.exit(1)
    catalog = read_catalog(catalog_path)
    problems = find_copy_problems(dest_dir, catalog)
    problems += find_summary_problems(HD_2013, catalog, read_tracked(args.tracked_manifest))
    for problem in problems:
        sys.stderr.write(f"FAIL: {problem}\n")
    if problems:
        sys.exit(1)
    print(f"OK: {catalog.total()} assets verified")


if __name__ == "__main__":
    main()
