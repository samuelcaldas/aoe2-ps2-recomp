#!/usr/bin/env python3
"""Import and validate local user-supplied PS2 executable.

Safe, idempotent import that validates file integrity before and after copy.
Never dumps binary content or modifies unapproved destinations.
"""
import argparse
import hashlib
import os
import shutil
import sys
from pathlib import Path

# Add repo root to sys.path
REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.target import SINGLE_TARGET

def compute_sha1(filepath: Path) -> str:
    """Compute lowercase SHA-1 checksum in streaming chunks."""
    h = hashlib.sha1()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().lower()

def parse_args():
    parser = argparse.ArgumentParser(description="Import owned PS2 ELF executable.")
    parser.add_argument(
        "--source",
        type=Path,
        required=True,
        help="Path to source AOK.ELF file",
    )
    parser.add_argument(
        "--dest-dir",
        type=Path,
        default=None,
        help="Destination directory (default: <repo_root>/game/SLES-50282)",
    )
    return parser.parse_args()

def main():
    args = parse_args()
    source_path = args.source.resolve()

    if not source_path.exists():
        sys.stderr.write(f"Error: Source file not found: {source_path}\n")
        sys.exit(1)

    if not source_path.is_file():
        sys.stderr.write(f"Error: Source is not a regular file: {source_path}\n")
        sys.exit(1)

    if source_path.name != SINGLE_TARGET.filename:
        sys.stderr.write(
            f"Error: Invalid source filename '{source_path.name}'. Expected '{SINGLE_TARGET.filename}'\n"
        )
        sys.exit(1)

    # Validate source SHA-1 before copying
    src_sha1 = compute_sha1(source_path)
    if src_sha1 != SINGLE_TARGET.expected_sha1:
        sys.stderr.write(
            f"Error: Source checksum mismatch.\n"
            f"Expected: {SINGLE_TARGET.expected_sha1}\n"
            f"Actual:   {src_sha1}\n"
        )
        sys.exit(1)

    # Destination directory resolution
    if args.dest_dir is not None:
        target_dir = args.dest_dir.resolve()
    else:
        target_dir = (REPO_ROOT / "game" / SINGLE_TARGET.serial).resolve()

    dest_file = target_dir / SINGLE_TARGET.filename

    # Guard: refuse unexpected directory name
    if target_dir.name != SINGLE_TARGET.serial:
        sys.stderr.write(
            f"Error: Refusing unexpected destination directory: {target_dir}. "
            f"Expected leaf directory '{SINGLE_TARGET.serial}'\n"
        )
        sys.exit(1)

    target_dir.mkdir(parents=True, exist_ok=True)

    # Check if destination exists
    if dest_file.exists():
        dest_sha1 = compute_sha1(dest_file)
        if dest_sha1 == SINGLE_TARGET.expected_sha1:
            print(f"Target already imported and valid at: {dest_file}")
            print(f"Size: {dest_file.stat().st_size} bytes")
            print(f"SHA-1: {dest_sha1}")
            sys.exit(0)
        else:
            sys.stderr.write(
                f"Error: Conflicting file exists at {dest_file} with unexpected checksum: {dest_sha1}\n"
                f"Refusing to overwrite conflicting file.\n"
            )
            sys.exit(1)

    # Perform copy
    shutil.copy2(source_path, dest_file)

    # Validate SHA-1 after copying
    post_sha1 = compute_sha1(dest_file)
    if post_sha1 != SINGLE_TARGET.expected_sha1:
        dest_file.unlink(missing_ok=True)
        sys.stderr.write(
            f"Error: Destination verification failed after copying: {post_sha1}\n"
        )
        sys.exit(1)

    print(f"Successfully imported {SINGLE_TARGET.title}")
    print(f"Destination: {dest_file}")
    print(f"Size: {dest_file.stat().st_size} bytes")
    print(f"SHA-1: {post_sha1}")
    sys.exit(0)

if __name__ == "__main__":
    main()
