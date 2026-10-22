"""Builds a deterministic mapping from imported HD files to provider asset paths."""

from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from tools.hd_assets.asset_catalog import AssetCatalog, AssetRecord
from tools.hd_assets.errors import HdAssetError

SUPPORTED_PLATFORMS = frozenset({"desktop", "android", "switch"})
STAGE_DIRECTORY = Path("build") / "hd-stage"
SOURCE_DIRECTORY = Path("game") / "hd-edition" / "source"
DESTINATION_CATEGORIES = {
    "interface": "interface",
    "graphics": "graphics",
    "terrain": "terrain",
    "gamedata": "gamedata",
    "datfile": "gamedata",
}


@dataclass(frozen=True)
class StageEntry:
    source: Path
    source_relative: str
    destination_relative: str
    asset_format: str
    size: int
    sha256: str


def stage_root_for(platform: str, repository_root: Path) -> Path:
    """Return the ignored data/package root for a supported platform."""
    _require_platform(platform)
    if platform != "switch":
        return repository_root / "game" / "SLES-50282"
    return repository_root / STAGE_DIRECTORY / platform


def plan_stage(
    catalog: AssetCatalog,
    platform: str,
    repository_root: Path,
    source_root: Path | None = None,
) -> tuple[StageEntry, ...]:
    """Map catalog rows to raw loose files under the provider's assets/category layout."""
    _require_platform(platform)
    root = source_root or repository_root / SOURCE_DIRECTORY
    entries = [_entry(record, root) for record in catalog.records]
    destinations = [entry.destination_relative for entry in entries]
    if len(destinations) != len(set(destinations)):
        raise HdAssetError("catalog contains duplicate destination asset paths")
    return tuple(sorted(entries, key=lambda entry: entry.destination_relative))


def _entry(record: AssetRecord, repository_root: Path) -> StageEntry:
    category = DESTINATION_CATEGORIES.get(record.category)
    if category is None:
        raise HdAssetError(f"unsupported source category in catalog: {record.category}")
    source_relative = _safe_relative(record.source_relative)
    filename = PurePosixPath(source_relative).name
    destination_relative = PurePosixPath("assets", category, filename).as_posix()
    return StageEntry(
        source=repository_root / Path(source_relative),
        source_relative=source_relative,
        destination_relative=destination_relative,
        asset_format=record.asset_format,
        size=record.size,
        sha256=record.sha256,
    )


def _safe_relative(value: str) -> str:
    path = PurePosixPath(value)
    if path.is_absolute() or not path.parts or any(part in {"", ".", ".."} for part in path.parts):
        raise HdAssetError(f"unsafe source path in catalog: {value}")
    if "\\" in value:
        raise HdAssetError(f"unsafe source path in catalog: {value}")
    return path.as_posix()


def _require_platform(platform: str) -> None:
    if platform not in SUPPORTED_PLATFORMS:
        raise HdAssetError(f"unsupported platform: {platform}")
