"""Checks that a source folder is a recognised install of the expected edition."""

from pathlib import Path

from core.hd_source import EditionProfile
from tools.hd_assets.errors import EditionMismatchError, HdAssetError, MissingCategoryError


def validate_source(source_root: Path, profile: EditionProfile, categories: tuple[str, ...]) -> None:
    """Raise unless the source is a directory with the edition's sentinels and each requested category."""
    if not categories:
        raise HdAssetError("no categories requested")
    if not source_root.is_dir():
        raise EditionMismatchError(f"source is not a directory: {source_root}")
    for sentinel in profile.sentinel_paths:
        if not (source_root / sentinel).is_file():
            raise EditionMismatchError(f"{profile.key} sentinel missing: {sentinel}")
    for category in categories:
        _require_category(source_root, profile, category)


def _require_category(source_root: Path, profile: EditionProfile, category: str) -> None:
    if category not in profile.category_names():
        raise HdAssetError(f"unknown category: {category}")
    if not (source_root / profile.path_for(category)).exists():
        raise MissingCategoryError(f"category missing from install: {category}")
