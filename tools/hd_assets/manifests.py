"""Local full catalog and the tracked, digest-only summary of an import."""

import json
from pathlib import Path

from core.hd_source import EditionProfile
from tools.hd_assets.asset_catalog import AssetCatalog
from tools.hd_assets.digest import sha256_of_digests

SCHEMA_VERSION = 1
TOOL_VERSION = "1.0.0"
TRACKED_KEYS = frozenset({"schema", "tool_version", "edition", "counts", "digests"})


def write_catalog(catalog: AssetCatalog, path: Path) -> None:
    _write_json(path, catalog.to_rows())


def read_catalog(path: Path) -> AssetCatalog:
    return AssetCatalog.from_rows(_read_json(path))


def build_tracked_summary(profile: EditionProfile, catalog: AssetCatalog) -> dict:
    return {
        "schema": SCHEMA_VERSION,
        "tool_version": TOOL_VERSION,
        "edition": profile.key,
        "counts": {
            "total": catalog.total(),
            "by_category": catalog.counts_by_category(),
            "by_format": catalog.counts_by_format(),
        },
        "digests": {
            category: sha256_of_digests(catalog.digests_in(category))
            for category in catalog.categories()
        },
    }


def write_tracked(summary: dict, path: Path) -> None:
    _write_json(path, summary)


def read_tracked(path: Path) -> dict:
    return _read_json(path)


def _write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def _read_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))
