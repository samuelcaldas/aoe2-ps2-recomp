#!/usr/bin/env python3
"""Wrap one disc overlay file in a minimal ELF32 MIPS image so ELF-only tools accept it.

The game's loader copies the whole overlay file (header included) to its load address,
so the image is one RWX PT_LOAD at that address holding the file bytes, with bss
as the memory-size tail. Output goes only to git-ignored paths (never commit it).
"""
from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))
from tools.map_overlays import HEADER_BYTES, OverlayMapError, parse_overlay_header  # noqa: E402
from tools.inventory_overlays import InventoryError, require_ignored  # noqa: E402

ELF_HEADER = struct.Struct("<16sHHIIIIIHHHHHH")
PROGRAM_HEADER = struct.Struct("<8I")
ELF_HEADER_BYTES = 52
PROGRAM_HEADER_BYTES = 32
EM_MIPS = 8
ET_EXEC = 2
PT_LOAD = 1
PF_RWX = 7
E_IDENT = b"\x7fELF\x01\x01\x01" + b"\0" * 9
CODE_OFFSET = 0x1000


class OverlayElfError(ValueError):
    pass


def overlay_to_elf(raw: bytes, code_only: bool = False, entry: int | None = None, text_bytes: int | None = None) -> bytes:
    """code_only keeps just header+text in the file image, so analysis never sees data as code.

    text_bytes overrides the header text size for an overlay whose code runs into its data section."""
    try:
        header = parse_overlay_header(raw)
    except OverlayMapError as error:
        raise OverlayElfError(str(error)) from error
    text = header.text if text_bytes is None else text_bytes
    if text % 4 != 0 or not 0 <= text <= len(raw) - HEADER_BYTES:
        raise OverlayElfError("text size must be 4-aligned and within the overlay file")
    loaded = raw[:HEADER_BYTES + text] if code_only else raw
    entry = header.load_address if entry is None else entry
    if entry % 4 != 0 or not header.load_address <= entry < header.load_address + HEADER_BYTES + text:
        raise OverlayElfError("entry must be a 4-aligned address inside the overlay text")
    memory = max(len(raw), HEADER_BYTES + header.text + header.data + header.bss)
    elf_header = ELF_HEADER.pack(E_IDENT, ET_EXEC, EM_MIPS, 1, entry, ELF_HEADER_BYTES, 0, 0,
                                 ELF_HEADER_BYTES, PROGRAM_HEADER_BYTES, 1, 0, 0, 0)
    program = PROGRAM_HEADER.pack(PT_LOAD, CODE_OFFSET, header.load_address, header.load_address,
                                  len(loaded), memory, PF_RWX, 0x1000)
    prefix = (elf_header + program).ljust(CODE_OFFSET, b"\0")
    return prefix + loaded


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("overlay", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--entry", type=lambda text: int(text, 0), default=None, help="function start used as ELF entry")
    parser.add_argument("--code-only", action="store_true", help="load only header and text")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    try:
        require_ignored(args.out)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_bytes(overlay_to_elf(args.overlay.read_bytes(), args.code_only, args.entry))
    except (OSError, OverlayElfError, InventoryError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    print(f"Wrote {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
