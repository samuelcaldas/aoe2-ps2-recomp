"""Re-checks the local copies, the catalog, and the tracked summary."""

from pathlib import Path

from core.hd_source import EditionProfile
from tools.hd_assets.asset_catalog import AssetCatalog, AssetRecord
from tools.hd_assets.digest import sha256_file
from tools.hd_assets.importer import SOURCE_FOLDER
from tools.hd_assets.manifests import TRACKED_KEYS, build_tracked_summary
from tools.hd_assets.stage_verifier import find_stage_problems


def find_copy_problems(dest_root: Path, catalog: AssetCatalog) -> list[str]:
    problems: list[str] = []
    for record in catalog.records:
        problems.extend(_record_problems(dest_root, record))
    return problems


def find_summary_problems(profile: EditionProfile, catalog: AssetCatalog, tracked: dict) -> list[str]:
    problems: list[str] = []
    unexpected = sorted(set(tracked) - TRACKED_KEYS)
    if unexpected:
        problems.append(f"tracked manifest has unexpected keys: {unexpected}")
    problems.extend(_path_like_problems(tracked))
    if tracked != build_tracked_summary(profile, catalog):
        problems.append("tracked manifest does not match the local catalog")
    return problems


def _record_problems(dest_root: Path, record: AssetRecord) -> list[str]:
    target = dest_root / SOURCE_FOLDER / record.source_relative
    if not target.is_file():
        return [f"missing copy: {record.source_relative}"]
    if target.stat().st_size != record.size or sha256_file(target) != record.sha256:
        return [f"digest mismatch: {record.source_relative}"]
    return []


def _path_like_problems(node) -> list[str]:
    if isinstance(node, dict):
        problems: list[str] = []
        for key, value in node.items():
            problems += _path_like_problems(key)
            problems += _path_like_problems(value)
        return problems
    if isinstance(node, str) and ("/" in node or "\\" in node):
        return ["tracked manifest contains a path-like key or value"]
    return []
