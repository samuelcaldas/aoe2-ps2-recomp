#!/usr/bin/env python3
"""Map the disc's overlay files to the ELF's executable reservations by header evidence.

Each overlay file starts with a 0x40-byte header: magic "MWo3", overlay id, load
address, text/data/bss sizes. A file maps to the reservation whose segment index
equals its id, provided the load address matches and text+data+bss fits the
reservation. Anything ambiguous or unmatched fails. Detailed paths and addresses go
only to a git-ignored report; stdout carries counts.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))
from core.target import SINGLE_TARGET  # noqa: E402
from tools.inventory_overlays import InventoryError, require_ignored, reservations  # noqa: E402

MAGIC = b"MWo3"
HEADER_BYTES = 0x40
HEADER = struct.Struct("<4s7I")


class OverlayMapError(ValueError):
    pass


@dataclass(frozen=True)
class OverlayHeader:
    overlay_id: int
    load_address: int
    text: int
    data: int
    bss: int

    @property
    def memory_bytes(self) -> int:
        return self.text + self.data + self.bss


@dataclass(frozen=True)
class OverlayMatch:
    file: str
    segment_index: int
    header: OverlayHeader


def parse_overlay_header(raw: bytes) -> OverlayHeader:
    if len(raw) < HEADER_BYTES:
        raise OverlayMapError("overlay file shorter than its header")
    magic, overlay_id, load, text, data, bss, _, _ = HEADER.unpack_from(raw)
    if magic != MAGIC:
        raise OverlayMapError("not an overlay file (bad magic)")
    if HEADER_BYTES + text + data > len(raw):
        raise OverlayMapError("overlay file shorter than its declared sections")
    return OverlayHeader(overlay_id, load, text, data, bss)


def _match_one(name: str, header: OverlayHeader, by_index: dict) -> OverlayMatch:
    reservation = by_index.get(header.overlay_id)
    if reservation is None:
        raise OverlayMapError(f"{name}: no reservation with segment index {header.overlay_id}")
    if reservation["virtual_address"] != header.load_address:
        raise OverlayMapError(f"{name}: load address differs from its reservation")
    if header.memory_bytes > reservation["memory_bytes"]:
        raise OverlayMapError(f"{name}: sections exceed the reservation")
    return OverlayMatch(name, header.overlay_id, header)


def match_overlays(files: dict, reserved: list) -> list:
    """Return one match per reservation, or raise if any file or reservation is left over."""
    by_index = {entry["segment_index"]: entry for entry in reserved}
    matches = []
    seen = set()
    for name, raw in sorted(files.items()):
        header = parse_overlay_header(raw)
        if header.overlay_id in seen:
            raise OverlayMapError(f"{name}: overlay id {header.overlay_id} is used by two files")
        seen.add(header.overlay_id)
        matches.append(_match_one(name, header, by_index))
    unmatched = set(by_index) - seen
    if unmatched:
        raise OverlayMapError(f"{len(unmatched)} reservation(s) have no overlay file")
    return matches


def read_overlay_files(directory: Path) -> dict:
    files = {}
    for path in sorted(directory.iterdir()):
        if path.suffix.upper() == ".BIN" and path.is_file() and not path.is_symlink():
            raw = path.read_bytes()
            if raw[:4] == MAGIC:
                files[path.name] = raw
    return files


def build_report(elf: Path, directory: Path) -> dict:
    raw_elf = elf.read_bytes()
    if hashlib.sha1(raw_elf).hexdigest() != SINGLE_TARGET.expected_sha1:
        raise OverlayMapError("ELF does not match pinned target SHA-1")
    files = read_overlay_files(directory)
    matches = match_overlays(files, reservations(raw_elf))
    return {
        "schema_version": 1,
        "elf_sha1": SINGLE_TARGET.expected_sha1,
        "overlays": [
            {"file": m.file, "segment_index": m.segment_index, "load_address": m.header.load_address,
             "text": m.header.text, "data": m.header.data, "bss": m.header.bss,
             "file_bytes": len(files[m.file]),
             "trailer_bytes": len(files[m.file]) - HEADER_BYTES - m.header.text - m.header.data, "sha256": hashlib.sha256(files[m.file]).hexdigest()}
            for m in matches
        ],
    }


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = REPO_ROOT / "game" / SINGLE_TARGET.serial
    parser.add_argument("--elf", type=Path, default=root / SINGLE_TARGET.filename)
    parser.add_argument("--overlay-dir", type=Path, default=root / Path(SINGLE_TARGET.disc_elf_path).parent)
    parser.add_argument("--out", type=Path, default=REPO_ROOT / "analysis/generated/overlay_map.json")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        require_ignored(args.out)
        report = build_report(args.elf, args.overlay_dir)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    except (OSError, OverlayMapError, InventoryError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    shared = len({entry["load_address"] for entry in report["overlays"]})
    print(f"Mapped {len(report['overlays'])} overlay file(s) to executable reservations "
          f"({shared} distinct load address(es)).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
