#!/usr/bin/env python3
"""Import an owned SLES-50282 disc image as the runtime's extracted CD root.

The runtime reads disc files relative to the ELF's directory, so the disc is
extracted into the ignored game/SLES-50282/ directory next to AOK.ELF. The
disc is accepted only if its English executable matches the target SHA-1.
Fail-fast and idempotent: a marker records the imported image's SHA-1, and an
existing AOK.ELF that differs from the disc's executable is never replaced.
Reports only paths, counts, and checksums.

Exit codes: 0 imported or already imported, 1 invalid input or mismatch.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.target import SINGLE_TARGET

MARKER_NAME = ".disc_import.json"
IMPORTED = "imported"
ALREADY_IMPORTED = "already_imported"
EXTRACTOR = "7z"


class DiscImportError(RuntimeError):
    """Raised when the disc image cannot be imported safely."""


def sha1_of_file(path: Path) -> str:
    digest = hashlib.sha1()
    with path.open("rb") as handle:
        while chunk := handle.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def sha1_of_disc_member(iso: Path, member: str) -> str:
    """Hash one file inside the image without extracting the whole disc."""
    result = subprocess.run([EXTRACTOR, "x", "-so", str(iso), member], capture_output=True)
    if result.returncode != 0 or not result.stdout:
        raise DiscImportError(f"disc image has no {member}: {iso}")
    return hashlib.sha1(result.stdout).hexdigest()


def read_marker(destination: Path) -> dict:
    marker = destination / MARKER_NAME
    if not marker.is_file():
        return {}
    return json.loads(marker.read_text(encoding="utf-8"))


def require_compatible_elf(destination: Path, expected_elf_sha1: str) -> None:
    existing = destination / SINGLE_TARGET.filename
    if existing.is_file() and sha1_of_file(existing) != expected_elf_sha1:
        raise DiscImportError(f"existing {existing} differs from the disc executable; refusing to replace it")


def extract_into(iso: Path, destination: Path, expected_elf_sha1: str) -> int:
    """Extract to a sibling staging directory, then move the tree into place."""
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent) as staging_name:
        staging = Path(staging_name)
        subprocess.run([EXTRACTOR, "x", "-y", f"-o{staging}", str(iso)], check=True, capture_output=True)
        staged_elf = staging / SINGLE_TARGET.disc_elf_path
        if sha1_of_file(staged_elf) != expected_elf_sha1:
            raise DiscImportError("extracted executable changed during import")
        shutil.copy2(staged_elf, staging / SINGLE_TARGET.filename)
        entries = list(staging.iterdir())
        for entry in entries:
            target = destination / entry.name
            if target.is_dir():
                shutil.rmtree(target)
            if target.is_file():
                target.unlink()
            shutil.move(str(entry), str(target))
    return len(entries)


def import_disc(iso: Path, destination: Path, expected_elf_sha1: str = SINGLE_TARGET.expected_sha1) -> str:
    """Import the disc image into destination; return IMPORTED or ALREADY_IMPORTED.

    Raises DiscImportError if the image is missing, holds a different game
    executable, or would overwrite a conflicting AOK.ELF.
    """
    if not iso.is_file():
        raise DiscImportError(f"disc image not found: {iso}")
    if sha1_of_disc_member(iso, SINGLE_TARGET.disc_elf_path) != expected_elf_sha1:
        raise DiscImportError(f"{SINGLE_TARGET.disc_elf_path} on the disc does not match the target SHA-1")
    iso_sha1 = sha1_of_file(iso)
    if read_marker(destination).get("iso_sha1") == iso_sha1:
        return ALREADY_IMPORTED
    require_compatible_elf(destination, expected_elf_sha1)
    entry_count = extract_into(iso, destination, expected_elf_sha1)
    marker = {"iso_sha1": iso_sha1, "elf_sha1": expected_elf_sha1, "top_level_entries": entry_count}
    (destination / MARKER_NAME).write_text(json.dumps(marker, indent=2) + "\n", encoding="utf-8")
    return IMPORTED


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description="Import the owned SLES-50282 disc image as the CD root.")
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--dest-dir", type=Path, default=REPO_ROOT / "game" / SINGLE_TARGET.serial)
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    if shutil.which(EXTRACTOR) is None:
        sys.stderr.write(f"Error: '{EXTRACTOR}' is required to read the disc image\n")
        return 1
    try:
        status = import_disc(args.iso.resolve(), args.dest_dir.resolve())
    except DiscImportError as error:
        sys.stderr.write(f"Error: {error}\n")
        return 1
    print(f"Disc {status}: {args.dest_dir.resolve()}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
