#!/usr/bin/env python3
"""Extract an owned SLES-50282 ISO9660 image into the runtime's CD root (stdlib only).

Writes the file tree, AOK.ELF, and disc_layout.json (path -> real LBN + size) so the
runtime serves LBN reads without the ISO. The disc is accepted only if EN/AGE2_PS2.ELF
matches the target SHA-1. Idempotent: a marker records the ISO SHA-1 and layout version.
The tree and manifest are derived from the disc and must stay git-ignored.

Exit codes: 0 extracted or already extracted, 1 invalid input.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.target import SINGLE_TARGET

SECTOR = 2048
PVD_LBN = 16
MARKER_NAME = ".disc_extract.json"
LAYOUT_NAME = "disc_layout.json"
LAYOUT_VERSION = 1
EXTRACTED = "extracted"
ALREADY_EXTRACTED = "already_extracted"
FLAG_DIRECTORY = 0x02


class DiscExtractError(RuntimeError):
    """Raised when the disc image cannot be extracted safely."""


def sha1_of_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as handle:
        while chunk := handle.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def _clean_name(raw: bytes) -> str:
    name = raw.decode("ascii", errors="replace")
    return name.split(";")[0].removesuffix(".")


def _read_directory(image, lbn: int, size: int, prefix: str):
    """Yield (path, lbn, size) for every file below the directory, recursively."""
    image.seek(lbn * SECTOR)
    data = image.read(size)
    offset = 0
    while offset < len(data):
        record_length = data[offset]
        if record_length == 0:
            offset = (offset // SECTOR + 1) * SECTOR
            continue
        extent, = struct.unpack_from("<I", data, offset + 2)
        length, = struct.unpack_from("<I", data, offset + 10)
        flags = data[offset + 25]
        name_length = data[offset + 32]
        raw_name = data[offset + 33: offset + 33 + name_length]
        offset += record_length
        if raw_name in (b"\x00", b"\x01"):
            continue
        name = _clean_name(raw_name)
        path = f"{prefix}{name}"
        if flags & FLAG_DIRECTORY:
            yield from _read_directory(image, extent, length, path + "/")
            continue
        yield path, extent, length


def list_disc_files(iso: Path) -> list:
    """Return [(path, lbn, size)] for every file on the image."""
    with iso.open("rb") as image:
        image.seek(PVD_LBN * SECTOR)
        pvd = image.read(SECTOR)
        if pvd[0] != 1 or pvd[1:6] != b"CD001":
            raise DiscExtractError(f"not an ISO9660 image: {iso}")
        root_extent, = struct.unpack_from("<I", pvd, 156 + 2)
        root_size, = struct.unpack_from("<I", pvd, 156 + 10)
        return list(_read_directory(image, root_extent, root_size, ""))


def _copy_file(image, lbn: int, size: int, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    image.seek(lbn * SECTOR)
    remaining = size
    with target.open("wb") as out:
        while remaining > 0:
            chunk = image.read(min(remaining, 1 << 20))
            if not chunk:
                raise DiscExtractError(f"image truncated while reading {target.name}")
            out.write(chunk)
            remaining -= len(chunk)


def write_layout(files: list, destination: Path) -> None:
    # One file object per line: the runtime parses the manifest line by line.
    lines = [
        json.dumps({"path": path, "lbn": lbn, "size": size})
        for path, lbn, size in sorted(files, key=lambda item: item[1])
    ]
    body = '{"version": %d, "files": [\n%s\n]}\n' % (LAYOUT_VERSION, ",\n".join(lines))
    (destination / LAYOUT_NAME).write_text(body, encoding="utf-8")


def read_marker(destination: Path) -> dict:
    marker = destination / MARKER_NAME
    return json.loads(marker.read_text(encoding="utf-8")) if marker.is_file() else {}


def is_up_to_date(destination: Path, iso_sha1: str) -> bool:
    marker = read_marker(destination)
    return (marker.get("iso_sha1") == iso_sha1
            and marker.get("layout_version") == LAYOUT_VERSION
            and (destination / LAYOUT_NAME).is_file()
            and (destination / SINGLE_TARGET.filename).is_file())


def extract_disc(iso: Path, destination: Path, expected_elf_sha1: str = SINGLE_TARGET.expected_sha1) -> str:
    """Extract iso into destination; return EXTRACTED or ALREADY_EXTRACTED."""
    if not iso.is_file():
        raise DiscExtractError(f"disc image not found: {iso}")
    files = list_disc_files(iso)
    elf_entry = next((f for f in files if f[0].lower() == SINGLE_TARGET.disc_elf_path.lower()), None)
    if elf_entry is None:
        raise DiscExtractError(f"disc image has no {SINGLE_TARGET.disc_elf_path}: {iso}")
    iso_sha1 = sha1_of_file(iso)
    if is_up_to_date(destination, iso_sha1):
        return ALREADY_EXTRACTED
    existing = destination / SINGLE_TARGET.filename
    if existing.is_file() and sha1_of_file(existing) != expected_elf_sha1:
        raise DiscExtractError(f"existing {existing} differs from the disc executable; refusing to replace it")
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as staging_name, iso.open("rb") as image:
        staging = Path(staging_name)
        for path, lbn, size in files:
            _copy_file(image, lbn, size, staging / path)
        staged_elf = staging / elf_entry[0]
        if sha1_of_file(staged_elf) != expected_elf_sha1:
            raise DiscExtractError(f"{SINGLE_TARGET.disc_elf_path} on the disc does not match the target SHA-1")
        shutil.copy2(staged_elf, staging / SINGLE_TARGET.filename)
        for entry in list(staging.iterdir()):
            target = destination / entry.name
            if target.is_dir():
                shutil.rmtree(target)
            elif target.exists():
                target.unlink()
            shutil.move(str(entry), str(target))
    write_layout(files, destination)
    marker = {"iso_sha1": iso_sha1, "elf_sha1": expected_elf_sha1,
              "layout_version": LAYOUT_VERSION, "file_count": len(files)}
    (destination / MARKER_NAME).write_text(json.dumps(marker, indent=2) + "\n", encoding="utf-8")
    return EXTRACTED


def find_iso(explicit: Path | None = None) -> Path | None:
    """Locate the owned ISO: explicit path, then game/iso/*.iso, then game/<serial>/aoe2.iso."""
    if explicit is not None:
        return explicit
    candidates = sorted((REPO_ROOT / "game" / "iso").glob("*.iso")) if (REPO_ROOT / "game" / "iso").is_dir() else []
    candidates.append(REPO_ROOT / "game" / SINGLE_TARGET.serial / "aoe2.iso")
    return next((path for path in candidates if path.is_file()), None)


def ensure_extracted(destination: Path, explicit_iso: Path | None = None) -> str:
    """Extract the disc if the CD root is missing or stale; fail early without an ISO."""
    iso = find_iso(explicit_iso)
    if iso is None:
        if (destination / LAYOUT_NAME).is_file() and (destination / SINGLE_TARGET.filename).is_file():
            return ALREADY_EXTRACTED
        raise DiscExtractError("no ISO found: put it in game/iso/ or pass --iso <path>")
    return extract_disc(iso.resolve(), destination)


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description="Extract the owned SLES-50282 ISO as the CD root.")
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--dest-dir", type=Path, default=REPO_ROOT / "game" / SINGLE_TARGET.serial)
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        status = extract_disc(args.iso.resolve(), args.dest_dir.resolve())
    except DiscExtractError as error:
        sys.stderr.write(f"Error: {error}\n")
        return 1
    print(f"Disc {status}: {args.dest_dir.resolve()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
