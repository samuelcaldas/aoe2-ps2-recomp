"""Renders a local, untracked report that answers open questions about the HD interface assets."""

from collections import Counter
from pathlib import Path

from tools.hd_assets.asset_catalog import AssetCatalog, AssetRecord
from tools.hd_assets.importer import SOURCE_FOLDER
from tools.hd_assets.slp_header import read_slp_size

SLP_2_0N = "slp-2.0N"


def render_inventory(catalog: AssetCatalog, dest_root: Path) -> str:
    lines = ["categories:"]
    lines += [f"  {name}: {count}" for name, count in catalog.counts_by_category().items()]
    lines.append("formats:")
    lines += [f"  {label}: {count}" for label, count in catalog.counts_by_format().items()]
    lines.append("numeric ids per category:")
    lines += _id_lines(catalog)
    lines.append("SLP 2.0N sizes per category (width x height):")
    lines += _size_lines(catalog, dest_root)
    return "\n".join(lines) + "\n"


def _id_lines(catalog: AssetCatalog) -> list[str]:
    lines = []
    for category in catalog.categories():
        ids = [record.asset_id for record in catalog.records_in(category) if record.asset_id is not None]
        if ids:
            lines.append(f"  {category}: {min(ids)}..{max(ids)} ({len(ids)} numeric)")
    return lines


def _size_lines(catalog: AssetCatalog, dest_root: Path) -> list[str]:
    lines = []
    for category in catalog.categories():
        sizes = Counter(_slp_size(dest_root, record) for record in _slp_records(catalog, category))
        if sizes:
            summary = ", ".join(f"{width}x{height} x{count}" for (width, height), count in sorted(sizes.items()))
            lines.append(f"  {category}: {summary}")
    return lines


def _slp_records(catalog: AssetCatalog, category: str) -> list[AssetRecord]:
    return [record for record in catalog.records_in(category) if record.asset_format == SLP_2_0N]


def _slp_size(dest_root: Path, record: AssetRecord) -> tuple[int, int]:
    size = read_slp_size(dest_root / SOURCE_FOLDER / record.source_relative)
    return (size.width, size.height)
