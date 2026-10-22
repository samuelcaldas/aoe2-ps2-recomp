"""MIPS (R5900) encodings and constant-load scanning for the KCET hook resolver.

The owned ELF is stripped, so the resolver finds code by what it loads and calls.
A 32-bit constant is built with `lui rt, hi` followed by `addiu rt, rt, lo` (signed
low half) or `ori rt, rt, lo` (unsigned low half) within a short window.
"""
from __future__ import annotations

import struct

from tools.kcet_resolver.elf_image import ElfImage

JR_RA = 0x03E00008
OP_JAL = 0x03
OP_ADDIU = 0x09
OP_ORI = 0x0D
OP_LUI = 0x0F
LOAD_WINDOW = 4
CALL_WINDOW = 8
WORD = struct.Struct("<I")


def encode_i(opcode: int, rs: int, rt: int, immediate: int) -> int:
    return (opcode << 26) | (rs << 21) | (rt << 16) | (immediate & 0xFFFF)


def lui(rt: int, immediate: int) -> int:
    return encode_i(OP_LUI, 0, rt, immediate)


def addiu(rt: int, rs: int, immediate: int) -> int:
    return encode_i(OP_ADDIU, rs, rt, immediate)


def ori(rt: int, rs: int, immediate: int) -> int:
    return encode_i(OP_ORI, rs, rt, immediate)


def jal(target: int) -> int:
    return (OP_JAL << 26) | ((target >> 2) & 0x03FFFFFF)


def opcode_of(word: int) -> int:
    return word >> 26


def rs_of(word: int) -> int:
    return (word >> 21) & 0x1F


def rt_of(word: int) -> int:
    return (word >> 16) & 0x1F


def immediate_of(word: int) -> int:
    return word & 0xFFFF


def signed16(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def _words(code: bytes) -> tuple:
    return struct.unpack(f"<{len(code) // 4}I", code[:len(code) // 4 * 4])


def _constant_built_by(hi_word: int, follower: int) -> int | None:
    """Value formed by a lui word followed by an addiu/ori word on the same register, or None."""
    register = rt_of(hi_word)
    if rs_of(follower) != register or rt_of(follower) != register:
        return None
    high = immediate_of(hi_word) << 16
    if opcode_of(follower) == OP_ADDIU:
        return (high + signed16(immediate_of(follower))) & 0xFFFFFFFF
    if opcode_of(follower) == OP_ORI:
        return high | immediate_of(follower)
    return None


def find_address_loads(image: ElfImage, address: int, window: int = LOAD_WINDOW) -> list:
    """Guest addresses of `lui` instructions that, with a following addiu/ori, build `address`."""
    found = []
    for base, code in image.code_segments():
        words = _words(code)
        for index, word in enumerate(words):
            if opcode_of(word) != OP_LUI:
                continue
            for follower_index in range(index + 1, min(index + 1 + window, len(words))):
                if _constant_built_by(word, words[follower_index]) == address:
                    found.append(base + 4 * index)
                    break
    return found


def _constant_built_into_any_register(hi_word: int, follower: int) -> int | None:
    """Like _constant_built_by, but the addiu/ori may write a different register than the lui."""
    if rs_of(follower) != rt_of(hi_word) or rt_of(follower) == 0:
        return None
    high = immediate_of(hi_word) << 16
    if opcode_of(follower) == OP_ADDIU:
        return (high + signed16(immediate_of(follower))) & 0xFFFFFFFF
    if opcode_of(follower) == OP_ORI:
        return high | immediate_of(follower)
    return None


def find_address_loads_any_register(image: ElfImage, address: int, window: int = LOAD_WINDOW) -> list:
    """Guest addresses of `lui` words whose base register feeds an addiu/ori that builds `address`."""
    found = []
    for base, code in image.code_segments():
        words = _words(code)
        for index, word in enumerate(words):
            if opcode_of(word) != OP_LUI:
                continue
            followers = words[index + 1:index + 1 + window]
            if any(_constant_built_into_any_register(word, follower) == address for follower in followers):
                found.append(base + 4 * index)
    return found


def words_between(image: ElfImage, start: int, end: int) -> tuple:
    """Instruction words of the guest range [start, end)."""
    return _words(image.read(start, end - start))


def constants_built(image: ElfImage, start: int, end: int, window: int = LOAD_WINDOW) -> list:
    """Distinct 32-bit constants that lui/addiu or lui/ori pairs build inside [start, end)."""
    words = words_between(image, start, end)
    values = []
    for index, word in enumerate(words):
        if opcode_of(word) != OP_LUI:
            continue
        for follower in words[index + 1:index + 1 + window]:
            value = _constant_built_by(word, follower)
            if value is not None and value not in values:
                values.append(value)
                break
    return values


def small_arguments_before_calls(image: ElfImage, start: int, end: int, register: int,
                                 window: int = CALL_WINDOW) -> list:
    """Immediates loaded into `register` with `addiu register, $zero, imm` that a jal follows within `window` words."""
    words = words_between(image, start, end)
    immediates = []
    for index, word in enumerate(words):
        if opcode_of(word) != OP_ADDIU or rs_of(word) != 0 or rt_of(word) != register:
            continue
        if any(opcode_of(following) == OP_JAL for following in words[index + 1:index + 1 + window]):
            immediates.append(immediate_of(word))
    return immediates
