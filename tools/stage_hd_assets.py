#!/usr/bin/env python3
"""Stage raw HD assets under the shared assets/<category>/ provider layout.

Desktop and Android share the ignored PS2 data root. Switch output is prepared
under build/hd-stage/switch for copying beside the NRO on the SD card.
"""
import argparse
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from tools.hd_assets.errors import HdAssetError
from tools.hd_assets.manifests import read_catalog
from tools.hd_assets.stage_plan import plan_stage, stage_root_for
from tools.hd_assets.staging import stage_assets

DEFAULT_CATALOG = REPO_ROOT / "game" / "hd-edition" / "catalog.json"
DEFAULT_SOURCE = REPO_ROOT / "game" / "hd-edition" / "source"


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--platform", required=True, choices=("desktop", "android", "switch"))
    parser.add_argument("--catalog", type=Path, default=DEFAULT_CATALOG)
    parser.add_argument("--source-dir", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--repo-root", type=Path, default=REPO_ROOT)
    parser.add_argument("--dry-run", action="store_true", help="print stage paths without copying files")
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    try:
        catalog = read_catalog(args.catalog)
        if args.dry_run:
            return _print_plan(catalog, args.platform, args.repo_root, args.source_dir)
        report = stage_assets(catalog, args.platform, args.source_dir, args.repo_root)
    except (HdAssetError, OSError, ValueError) as error:
        sys.stderr.write(f"Error: {error}\n")
        return 1
    print(f"Staged {report.entry_count} raw assets for {report.platform} at {report.relative_root}")
    return 0



def _print_plan(catalog, platform: str, repository_root: Path, source_root: Path) -> int:
    for entry in plan_stage(catalog, platform, repository_root, source_root):
        print(f"{entry.source} -> {stage_root_for(platform, repository_root) / entry.destination_relative}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
