"""Catalog of the files in the requested categories of a local install."""

from collections import Counter
from dataclasses import asdict, dataclass
from pathlib import Path

from core.hd_source import EditionProfile
from tools.hd_assets.digest import sha256_file
from tools.hd_assets.format_sniffer import sniff_format

HEADER_BYTES = 32


@dataclass(frozen=True)
class AssetRecord:
    category: str
    source_relative: str
    asset_id: int | None
    asset_format: str
    size: int
    sha256: str


class AssetCatalog:
    """Ordered asset records with per-category and per-format summaries."""

    def __init__(self, records: tuple[AssetRecord, ...]) -> None:
        self._records = records

    @classmethod
    def scan(cls, source_root: Path, profile: EditionProfile, categories: tuple[str, ...]) -> "AssetCatalog":
        records = []
        for category in categories:
            records.extend(_scan_category(source_root, profile, category))
        return cls(tuple(records))

    @classmethod
    def from_rows(cls, rows: list[dict]) -> "AssetCatalog":
        return cls(tuple(AssetRecord(**row) for row in rows))

    @property
    def records(self) -> tuple[AssetRecord, ...]:
        return self._records

    def to_rows(self) -> list[dict]:
        return [asdict(record) for record in self._records]

    def merged_with(self, other: "AssetCatalog") -> "AssetCatalog":
        by_path = {record.source_relative: record for record in self._records}
        by_path.update({record.source_relative: record for record in other.records})
        ordered = sorted(by_path.values(), key=lambda record: (record.category, record.source_relative))
        return AssetCatalog(tuple(ordered))

    def total(self) -> int:
        return len(self._records)

    def categories(self) -> tuple[str, ...]:
        return tuple(sorted({record.category for record in self._records}))

    def records_in(self, category: str) -> tuple[AssetRecord, ...]:
        return tuple(record for record in self._records if record.category == category)

    def digests_in(self, category: str) -> tuple[str, ...]:
        return tuple(record.sha256 for record in self.records_in(category))

    def counts_by_category(self) -> dict[str, int]:
        return _count(record.category for record in self._records)

    def counts_by_format(self) -> dict[str, int]:
        return _count(record.asset_format for record in self._records)


def _scan_category(source_root: Path, profile: EditionProfile, category: str) -> list[AssetRecord]:
    anchor = source_root / profile.path_for(category)
    return [_describe(category, source_root, path) for path in _files_under(anchor)]


def _files_under(anchor: Path) -> list[Path]:
    if anchor.is_file():
        return [anchor]
    return sorted(path for path in anchor.rglob("*") if path.is_file() and not path.is_symlink())


def _describe(category: str, source_root: Path, path: Path) -> AssetRecord:
    with path.open("rb") as handle:
        header = handle.read(HEADER_BYTES)
    return AssetRecord(
        category=category,
        source_relative=path.relative_to(source_root).as_posix(),
        asset_id=_asset_id(path),
        asset_format=sniff_format(header),
        size=path.stat().st_size,
        sha256=sha256_file(path),
    )


def _asset_id(path: Path) -> int | None:
    stem = path.stem
    return int(stem) if stem.isdecimal() else None


def _count(labels) -> dict[str, int]:
    return dict(sorted(Counter(labels).items()))
