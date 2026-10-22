#!/usr/bin/env python3
"""Tests for the ELF reader and MIPS reference scanner used by the KCET hook resolver."""
import struct
import unittest

from tools.kcet_resolver.elf_image import ElfFormatError, ElfImage
from tools.kcet_resolver.errors import HookResolutionError
from tools.kcet_resolver.functions import FunctionMap
from tools.kcet_resolver.mips import (
    JR_RA,
    addiu,
    find_address_loads,
    lui,
    ori,
)

PT_LOAD = 1
PF_RX = 5
PF_RW = 6


def build_elf(segments: list, machine: int = 8, elf_class: int = 1, endian: int = 1) -> bytes:
    """Build a little-endian ELF32 MIPS image with PT_LOAD segments of (vaddr, file bytes, memsz, flags)."""
    header_size, phentsize = 52, 32
    body = bytearray()
    phdrs = bytearray()
    offset = header_size + phentsize * len(segments)
    for vaddr, data, memsz, flags in segments:
        phdrs += struct.pack("<IIIIIIII", PT_LOAD, offset, vaddr, vaddr, len(data), memsz, flags, 0x10)
        body += data
        offset += len(data)
    ident = b"\x7fELF" + bytes([elf_class, endian, 1]) + bytes(9)
    header = ident + struct.pack("<HHIIIIIHHHHHH", 2, machine, 1, 0x100000, header_size, 0, 0,
                                 header_size, phentsize, len(segments), 40, 0, 0)
    return bytes(header + phdrs + body)


def code(*words: int) -> bytes:
    return b"".join(struct.pack("<I", word) for word in words)


class TestElfImage(unittest.TestCase):
    def setUp(self):
        self.image = ElfImage(build_elf([
            (0x100000, b"kt_mouse.c\x00" + bytes(3), 16, PF_RX),
            (0x200000, b"", 0x400, PF_RW),
        ]))

    def test_reads_file_backed_bytes_at_virtual_address(self):
        self.assertEqual(self.image.read(0x100000, 4), b"kt_m")

    def test_bss_and_overlay_segments_have_no_file_bytes(self):
        self.assertFalse(self.image.is_file_backed(0x200000, 4))
        with self.assertRaises(ElfFormatError):
            self.image.read(0x200000, 4)

    def test_reads_crossing_past_segment_end_fail(self):
        with self.assertRaises(ElfFormatError):
            self.image.read(0x100000 + 10, 8)

    def test_finds_every_occurrence_of_a_string(self):
        self.assertEqual(self.image.find_bytes(b"kt_mouse.c\x00"), [0x100000])

    def test_rejects_non_elf_input(self):
        with self.assertRaises(ElfFormatError):
            ElfImage(b"not an elf file at all, just text bytes here")

    def test_rejects_elf64(self):
        with self.assertRaises(ElfFormatError):
            ElfImage(build_elf([(0x100000, b"\0" * 4, 4, PF_RX)], elf_class=2))

    def test_rejects_big_endian(self):
        with self.assertRaises(ElfFormatError):
            ElfImage(build_elf([(0x100000, b"\0" * 4, 4, PF_RX)], endian=2))

    def test_rejects_non_mips_machine(self):
        with self.assertRaises(ElfFormatError):
            ElfImage(build_elf([(0x100000, b"\0" * 4, 4, PF_RX)], machine=62))


class TestCallableFunctionMap(unittest.TestCase):
    def test_continuation_does_not_shadow_callable_range(self):
        outer = (0x4000, 0x4080, "callable_handler")
        for name in ("entry_00004030", "caseD_1_00004030"):
            with self.subTest(name=name):
                functions = FunctionMap.from_rows([outer, (0x4030, 0x4080, name)])
                self.assertEqual(functions.containing(0x4040), outer)

    def test_continuation_is_not_a_callable_start(self):
        functions = FunctionMap.from_rows([
            (0x4000, 0x4080, "callable_handler"),
            (0x4030, 0x4080, "entry_00004030"),
        ])
        with self.assertRaises(HookResolutionError):
            functions.require_start(0x4030)

    def test_orphan_continuation_is_not_callable(self):
        functions = FunctionMap.from_rows([(0x4030, 0x4080, "entry_00004030")])
        self.assertIsNone(functions.containing(0x4040))

    def test_named_functions_and_thunks_remain_callable(self):
        rows = [(0x4000, 0x4080, "callable_handler"), (0x4080, 0x4090, "thunk_handler")]
        functions = FunctionMap.from_rows(rows)
        for row in rows:
            self.assertEqual(functions.containing(row[0]), row)
            functions.require_start(row[0])


class TestMipsAddressLoads(unittest.TestCase):
    def test_lui_ori_pair_loads_an_exact_constant(self):
        # SIF RPC service id 0x80000210 is loaded with lui/ori on the 4th register.
        image = ElfImage(build_elf([(0x100000, code(lui(5, 0x8000), ori(5, 5, 0x0210), JR_RA), 12, PF_RX)]))
        self.assertEqual(find_address_loads(image, 0x80000210), [0x100000])

    def test_lui_addiu_pair_uses_sign_extended_low_half(self):
        # 0x00128000 needs hi=0x0013 because addiu sign-extends 0x8000 to -0x8000.
        image = ElfImage(build_elf([(0x100000, code(lui(4, 0x0013), addiu(4, 4, 0x8000)), 8, PF_RX)]))
        self.assertEqual(find_address_loads(image, 0x00128000), [0x100000])

    def test_mismatched_register_is_not_a_load(self):
        image = ElfImage(build_elf([(0x100000, code(lui(5, 0x8000), ori(6, 6, 0x0210)), 8, PF_RX)]))
        self.assertEqual(find_address_loads(image, 0x80000210), [])

    def test_unrelated_constant_is_not_matched(self):
        image = ElfImage(build_elf([(0x100000, code(lui(5, 0x8000), ori(5, 5, 0x0211)), 8, PF_RX)]))
        self.assertEqual(find_address_loads(image, 0x80000210), [])


if __name__ == "__main__":
    unittest.main()
