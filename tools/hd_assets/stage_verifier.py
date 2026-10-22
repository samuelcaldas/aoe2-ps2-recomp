"""Verifies staged files against their ignored local manifest."""

import json
from pathlib import Path, PurePosixPath

from tools.hd_assets.digest import sha256_file
from tools.hd_assets.errors import HdAssetError
from tools.hd_assets.stage_plan import stage_root_for
from tools.hd_assets.staging import MANIFEST_NAME, MANIFEST_SCHEMA


def find_stage_problems(platform: str, repository_root: Path) -> list[str]:
    """Return every missing, changed, unexpected, or untracked staged-file problem."""
    try:
        stage_root = stage_root_for(platform, repository_root)
    except HdAssetError:
        return [f"unsupported platform: {platform}"]
    manifest_path = stage_root / MANIFEST_NAME
    if not manifest_path.is_file():
        return [f"stage manifest missing: {manifest_path}"]
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return ["stage manifest is unreadable"]
    problems = _validate_header(manifest, platform)
    entries = manifest.get("entries") if isinstance(manifest, dict) else None
    if not isinstance(entries, list):
        return problems + ["stage manifest entries must be a list"]
    problems += _entry_problems(stage_root, entries)
    problems += _extra_file_problems(stage_root, entries)
    return problems


def _validate_header(manifest, platform: str) -> list[str]:
    if not isinstance(manifest, dict) or set(manifest) != {"schema", "platform", "entries"}:
        return ["stage manifest has unexpected fields"]
    problems = []
    expected_platform = "portable" if platform in {"desktop", "android"} else platform
    if manifest["schema"] != MANIFEST_SCHEMA or manifest["platform"] != expected_platform:
        problems.append("stage manifest schema or platform mismatch")
    return problems


def _entry_problems(stage_root: Path, entries: list) -> list[str]:
    problems = []
    seen = set()
    for entry in entries:
        path = _entry_path(entry)
        if path is None:
            problems.append("stage manifest contains an invalid path")
            continue
        if path in seen:
            problems.append("stage manifest contains duplicate paths")
            continue
        seen.add(path)
        target = stage_root / Path(path)
        if not target.resolve().is_relative_to(stage_root.resolve()):
            problems.append(f"staged path escapes stage root: {path}")
            continue
        problems += _file_problems(target, entry)
    return problems


def _entry_path(entry) -> str | None:
    if not isinstance(entry, dict) or set(entry) != {"path", "size", "sha256"}:
        return None
    value = entry["path"]
    if not isinstance(value, str):
        return None
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or any(part in {"", ".", ".."} for part in path.parts):
        return None
    if path.parts[0] != "assets" or "\\" in value:
        return None
    return path.as_posix()


def _file_problems(path: Path, entry: dict) -> list[str]:
    if not path.is_file():
        return [f"missing copy: {entry['path']}"]
    if not isinstance(entry["size"], int) or path.stat().st_size != entry["size"]:
        return [f"size mismatch: {entry['path']}"]
    if not _valid_digest(entry["sha256"]) or sha256_file(path) != entry["sha256"]:
        return [f"digest mismatch: {entry['path']}"]
    return []


def _valid_digest(value) -> bool:
    return isinstance(value, str) and len(value) == 64 and all(char in "0123456789abcdef" for char in value)


def _extra_file_problems(stage_root: Path, entries: list) -> list[str]:
    expected = {entry.get("path") for entry in entries if isinstance(entry, dict)}
    assets_root = stage_root / "assets"
    if not assets_root.exists():
        return []
    actual = {path.relative_to(stage_root).as_posix() for path in assets_root.rglob("*") if path.is_file()}
    if actual == expected:
        return []
    return ["staged assets contain unlisted files"]
