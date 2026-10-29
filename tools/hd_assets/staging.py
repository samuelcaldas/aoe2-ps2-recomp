"""Copies raw catalog assets to an ignored platform data/package tree."""

import fcntl
import json
import os
import shutil
from dataclasses import dataclass
from pathlib import Path

from tools.hd_assets.digest import sha256_file
from tools.hd_assets.errors import ConflictError, HdAssetError, IntegrityError
from tools.hd_assets.stage_plan import StageEntry, plan_stage, stage_root_for

MANIFEST_NAME = "hd_stage.json"
MANIFEST_SCHEMA = 1
PARTIAL_SUFFIX = ".partial"
LOCK_NAME = ".staging.lock"


@dataclass(frozen=True)
class StageReport:
    platform: str
    relative_root: Path
    entry_count: int


def stage_assets(catalog, platform: str, source_root: Path, repository_root: Path) -> StageReport:
    """Stage original catalog bytes, persist a local manifest, and return the output root."""
    stage_root = stage_root_for(platform, repository_root)
    _validate_stage_root(repository_root, stage_root)
    stage_root.mkdir(parents=True, exist_ok=True)
    lock_path = stage_root / LOCK_NAME
    with lock_path.open("w") as lock_file:
        fcntl.flock(lock_file.fileno(), fcntl.LOCK_EX)
        try:
            entries = plan_stage(catalog, platform, repository_root, source_root)
            _reject_unlisted_files(stage_root, entries)
            rows = [_stage_entry(entry, stage_root, source_root) for entry in entries]
            _write_manifest(stage_root, platform, rows)
            relative_root = stage_root.relative_to(repository_root)
            return StageReport(platform=platform, relative_root=relative_root, entry_count=len(rows))
        finally:
            fcntl.flock(lock_file.fileno(), fcntl.LOCK_UN)


def _reject_unlisted_files(stage_root: Path, entries: tuple[StageEntry, ...]) -> None:
    assets_root = stage_root / "assets"
    if not assets_root.exists():
        return
    if not assets_root.resolve().is_relative_to(stage_root.resolve()):
        raise HdAssetError("assets directory escapes stage root")
    expected = {entry.destination_relative for entry in entries}
    actual = {
        path.relative_to(stage_root).as_posix()
        for path in assets_root.rglob("*")
        if path.is_file() or path.is_symlink()
    }
    if actual - expected:
        raise HdAssetError(f"stage contains {len(actual - expected)} unlisted files")


def _stage_entry(entry: StageEntry, stage_root: Path, source_root: Path) -> dict:
    _verify_source(entry, source_root)
    destination = stage_root / Path(entry.destination_relative)
    _copy_or_validate(entry, destination, stage_root)
    return {
        "path": entry.destination_relative,
        "size": entry.size,
        "sha256": entry.sha256,
    }


def _verify_source(entry: StageEntry, source_root: Path) -> None:
    resolved_source = entry.source.resolve()
    if not resolved_source.is_relative_to(source_root.resolve()):
        raise HdAssetError(f"catalog source escapes source root: {entry.source_relative}")
    if not resolved_source.is_file():
        raise HdAssetError(f"catalog source file is missing: {entry.source_relative}")
    if resolved_source.stat().st_size != entry.size or sha256_file(resolved_source) != entry.sha256:
        raise IntegrityError(f"catalog source digest mismatch: {entry.source_relative}")


def _copy_or_validate(entry: StageEntry, destination: Path, stage_root: Path) -> None:
    _validate_destination(stage_root, destination)
    if destination.exists():
        _require_same_digest(destination, entry)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    partial = destination.with_name(destination.name + PARTIAL_SUFFIX)
    descriptor = _create_partial(partial)
    try:
        with os.fdopen(descriptor, "wb") as output, entry.source.open("rb") as source:
            shutil.copyfileobj(source, output)
        _require_same_digest(partial, entry)
        partial.replace(destination)
    finally:
        partial.unlink(missing_ok=True)


def _create_partial(path: Path) -> int:
    try:
        return os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    except FileExistsError as error:
        raise ConflictError(f"staging partial file already exists: {path.name}") from error


def _validate_stage_root(repository_root: Path, stage_root: Path) -> None:
    if not stage_root.resolve().is_relative_to(repository_root.resolve()):
        raise HdAssetError("stage root escapes repository root")


def _validate_destination(stage_root: Path, destination: Path) -> None:
    stage_root.mkdir(parents=True, exist_ok=True)
    root = stage_root.resolve()
    if not destination.parent.resolve().is_relative_to(root):
        raise HdAssetError("destination parent escapes stage root")
    if destination.exists() and not destination.resolve().is_relative_to(root):
        raise HdAssetError("destination file escapes stage root")


def _require_same_digest(path: Path, entry: StageEntry) -> None:
    if not path.is_file() or path.stat().st_size != entry.size or sha256_file(path) != entry.sha256:
        raise ConflictError(f"staged file differs from catalog: {entry.destination_relative}")


def _write_manifest(stage_root: Path, platform: str, rows: list[dict]) -> None:
    stage_root.mkdir(parents=True, exist_ok=True)
    manifest = {
        "schema": MANIFEST_SCHEMA,
        "platform": _manifest_platform(platform),
        "entries": sorted(rows, key=lambda row: row["path"]),
    }
    manifest_path = stage_root / MANIFEST_NAME
    if manifest_path.exists():
        _validate_manifest_shape(json.loads(manifest_path.read_text(encoding="utf-8")))
    temporary = manifest_path.with_name(manifest_path.name + PARTIAL_SUFFIX)
    descriptor = _create_partial(temporary)
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8") as output:
            output.write(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
        temporary.replace(manifest_path)
    finally:
        temporary.unlink(missing_ok=True)


def _manifest_platform(platform: str) -> str:
    return "portable" if platform in {"desktop", "android"} else platform


def _validate_manifest_shape(manifest: dict) -> None:
    if set(manifest) != {"schema", "platform", "entries"}:
        raise ConflictError("existing stage manifest has unexpected fields")
