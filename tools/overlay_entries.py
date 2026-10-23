"""Find function entries of overlay code from calls made by the main program.

An overlay's internal functions are found by Ghidra, but functions nothing inside the
overlay references (entry points called from the main code) are invisible to a raw
import. The main code reaches overlays with direct jal/j to fixed addresses; those
targets, restricted to each overlay's text range, seed the analysis.
"""
from __future__ import annotations

import struct

from tools.kcet_resolver.elf_image import ElfImage

OPCODE_J = 2
OPCODE_JAL = 3
REGION_MASK = 0xF0000000


def entry_targets(image: ElfImage, ranges: list) -> list:
    """Sorted distinct jal/j targets inside any (start, end) range, from all executable segments."""
    found = set()
    for base, data in image.code_segments():
        for index in range(0, len(data) - 3, 4):
            word = struct.unpack_from("<I", data, index)[0]
            if word >> 26 not in (OPCODE_J, OPCODE_JAL):
                continue
            target = ((base + index + 4) & REGION_MASK) | ((word & 0x3FFFFFF) << 2)
            if any(start <= target < end for start, end in ranges):
                found.add(target)
    return sorted(found)


def seeds_for(targets: list, load_address: int, header_bytes: int, text_bytes: int) -> list:
    """Targets that fall in this overlay's text (after its header)."""
    start = load_address + header_bytes
    return sorted(target for target in targets if start <= target < start + text_bytes)
