#!/usr/bin/env python3
"""Inventory size-matched overlay candidates; never infer a loader mapping.

Detailed paths and addresses go only to a git-ignored report. The target ELF
must pass its pinned hash check. Candidates require exact reservation sizes;
compressed overlays or files with BSS require separate loader analysis.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))
from core.target import SINGLE_TARGET


class InventoryError(ValueError):
    pass


def reservations(raw: bytes) -> list[dict]:
    """Read nonempty executable, non-file-backed ELF32 MIPS reservations."""
    if len(raw) < 52 or raw[:7] != b'\x7fELF\x01\x01\x01':
        raise InventoryError('expected ELF32 little-endian version 1')
    machine = struct.unpack_from('<H', raw, 18)[0]
    if machine != 8 or struct.unpack_from('<I', raw, 20)[0] != 1:
        raise InventoryError('expected MIPS ELF version 1')
    offset = struct.unpack_from('<I', raw, 28)[0]
    stride, count = struct.unpack_from('<HH', raw, 42)
    if not count or stride < 32 or offset < 52 or offset + stride * count > len(raw):
        raise InventoryError('invalid program header table')
    result = []
    for index in range(count):
        kind, file_offset, address, _, filesz, memsz, flags, _ = struct.unpack_from('<8I', raw, offset + stride * index)
        if kind != 1:
            continue
        if filesz > memsz or file_offset + filesz > len(raw) or address + memsz > 2**32:
            raise InventoryError('invalid load segment range')
        if flags & 1 and filesz == 0 and memsz:
            result.append({'segment_index': index, 'virtual_address': address, 'memory_bytes': memsz})
    return result


def inventory(elf: Path, directory: Path, expected_sha1: str = SINGLE_TARGET.expected_sha1) -> dict:
    raw = elf.read_bytes()
    if hashlib.sha1(raw).hexdigest() != expected_sha1:
        raise InventoryError('ELF does not match pinned target SHA-1')
    if not directory.is_dir() or directory.is_symlink():
        raise InventoryError('candidate directory must be a real directory')
    candidates = []
    for path in sorted(directory.iterdir()):
        if path.suffix.upper() != '.BIN':
            continue
        if path.is_symlink() or not path.is_file():
            raise InventoryError('candidate must be a regular file')
        data = path.read_bytes()
        candidates.append({'path': path.name, 'size_bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    entries = reservations(raw)
    for entry in entries:
        entry['size_candidates'] = [item for item in candidates if item['size_bytes'] == entry['memory_bytes']]
    return {'schema_version': 1, 'elf_sha1': expected_sha1,
            'mapping_verified': False, 'reservations': entries}


def require_ignored(path: Path) -> None:
    try:
        relative = path.resolve().relative_to(REPO_ROOT)
    except ValueError as error:
        raise InventoryError('report must be inside the repository') from error
    result = subprocess.run(['git', 'check-ignore', '--quiet', '--', str(relative)], cwd=REPO_ROOT)
    if result.returncode != 0:
        raise InventoryError('report must be git-ignored')


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    root = REPO_ROOT / 'game' / SINGLE_TARGET.serial
    parser.add_argument('--elf', type=Path, default=root / SINGLE_TARGET.filename)
    parser.add_argument('--candidate-dir', type=Path, default=root / Path(SINGLE_TARGET.disc_elf_path).parent)
    parser.add_argument('--out', type=Path, default=REPO_ROOT / 'analysis/generated/overlay_inventory.json')
    args = parser.parse_args(argv)
    try:
        require_ignored(args.out)
        report = inventory(args.elf, args.candidate_dir)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    except (OSError, InventoryError) as error:
        print(f'Error: {error}', file=sys.stderr)
        return 1
    entries = report['reservations']
    matched = sum(bool(entry['size_candidates']) for entry in entries)
    print(f'Executable reservations: {len(entries)}; exact-size candidates: {matched}; unresolved: {len(entries) - matched}.')
    print('Loader mapping is unverified; no code generated or executed.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
