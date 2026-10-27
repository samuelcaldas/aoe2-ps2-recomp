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


OPCODE_LUI = 0x0F
OPCODE_ADDIU = 0x09
OPCODE_ORI = 0x0D
LOOKAHEAD = 4
MIN_TABLE_ENTRIES = 2


def _signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def _referenced_addresses(words: list) -> set:
    """Addresses built by a lui followed (within a few instructions) by addiu/ori on the same register."""
    found = set()
    for index, word in enumerate(words):
        if word >> 26 != OPCODE_LUI:
            continue
        register, high = (word >> 16) & 31, (word & 0xFFFF) << 16
        for follower in words[index + 1:index + 1 + LOOKAHEAD]:
            opcode, source = follower >> 26, (follower >> 21) & 31
            if source != register or opcode not in (OPCODE_ADDIU, OPCODE_ORI):
                continue
            low = _signed16(follower & 0xFFFF) if opcode == OPCODE_ADDIU else follower & 0xFFFF
            found.add((high + low) & 0xFFFFFFFF)
    return found


def switch_targets(raw: bytes, load_address: int, header_bytes: int, text_bytes: int) -> list:
    """Code addresses stored in jump tables the overlay itself references.

    A raw import cannot see switch cases: nothing branches to them, only a table
    in the data lists them. A table starts at an address the text builds with
    lui+addiu/ori and runs while the words are 4-aligned addresses inside the text.
    """
    words = [int.from_bytes(raw[i:i + 4], "little") for i in range(0, len(raw) - 3, 4)]
    text_start, text_end = load_address + header_bytes, load_address + header_bytes + text_bytes
    targets = set()
    for address in _referenced_addresses(words[:(header_bytes + text_bytes) // 4]):
        index = (address - load_address) // 4
        if address % 4 or address < text_end or index >= len(words):
            continue
        run = []
        while index < len(words) and text_start <= words[index] < text_end and words[index] % 4 == 0:
            run.append(words[index])
            index += 1
        if len(run) >= MIN_TABLE_ENTRIES:
            targets.update(run)
    return sorted(targets)
