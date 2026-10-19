#!/usr/bin/env python3
"""Verify local user-supplied PS2 executable.

Returns 0 if the target ELF exists and matches expected SHA-1, non-zero otherwise.
Never prints or dumps binary content.
"""
import argparse
import hashlib
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
    parser = argparse.ArgumentParser(description="Verify owned PS2 ELF executable.")
    parser.add_argument(
        "--game-dir",
        type=Path,
        default=None,
        help="Game root directory (default: <repo_root>/game)",
    )
    return parser.parse_args()

def main():
    args = parse_args()
    if args.game_dir is not None:
        game_root = args.game_dir.resolve()
    else:
        game_root = (REPO_ROOT / "game").resolve()

    target_elf = game_root / SINGLE_TARGET.serial / SINGLE_TARGET.filename

    if not target_elf.exists():
        sys.stderr.write(
            f"Error: Target ELF is missing at expected path: {target_elf}\n"
            f"Please run tools/import_owned_game.py --source <path-to-AOK.ELF> first.\n"
        )
        sys.exit(1)

    if not target_elf.is_file():
        sys.stderr.write(f"Error: Target path is not a regular file: {target_elf}\n")
        sys.exit(1)

    actual_sha1 = compute_sha1(target_elf)
    if actual_sha1 != SINGLE_TARGET.expected_sha1:
        sys.stderr.write(
            f"Error: Target ELF checksum mismatch for {target_elf}\n"
            f"Expected SHA-1: {SINGLE_TARGET.expected_sha1}\n"
            f"Actual SHA-1:   {actual_sha1}\n"
        )
        sys.exit(1)

    print(f"Verification SUCCESS: {SINGLE_TARGET.title}")
    print(f"Path:   {target_elf}")
    print(f"Size:   {target_elf.stat().st_size} bytes")
    print(f"SHA-1:  {actual_sha1}")
    sys.exit(0)

if __name__ == "__main__":
    main()
