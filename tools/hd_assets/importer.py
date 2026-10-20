"""Copies the requested categories of a local install into the ignored game folder."""

import shutil
from pathlib import Path

from core.hd_source import EditionProfile
from tools.hd_assets.asset_catalog import AssetCatalog, AssetRecord
from tools.hd_assets.digest import sha256_file
from tools.hd_assets.edition_detector import validate_source
from tools.hd_assets.errors import ConflictError, IntegrityError

SOURCE_FOLDER = "source"
PARTIAL_SUFFIX = ".partial"


def import_assets(
    source_root: Path, profile: EditionProfile, categories: tuple[str, ...], dest_root: Path
) -> AssetCatalog:
    """Copy each requested file, verify its digest, and return the catalog of the copies."""
    validate_source(source_root, profile, categories)
    catalog = AssetCatalog.scan(source_root, profile, categories)
    for record in catalog.records:
        _copy_record(source_root, record, dest_root)
    return catalog


def _copy_record(source_root: Path, record: AssetRecord, dest_root: Path) -> None:
    target = dest_root / SOURCE_FOLDER / record.source_relative
    if target.exists():
        _require_matching_copy(target, record)
        return
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + PARTIAL_SUFFIX)
    try:
        shutil.copyfile(source_root / record.source_relative, partial)
        _require_digest(partial, record)
        partial.replace(target)
    finally:
        partial.unlink(missing_ok=True)


def _require_matching_copy(target: Path, record: AssetRecord) -> None:
    if sha256_file(target) != record.sha256:
        raise ConflictError(f"destination differs from source: {record.source_relative}")


def _require_digest(copied: Path, record: AssetRecord) -> None:
    if sha256_file(copied) != record.sha256:
        raise IntegrityError(f"copy does not match source digest: {record.source_relative}")
