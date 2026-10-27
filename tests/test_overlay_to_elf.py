import unittest

from tests.test_map_overlays import LOAD, overlay
from tools.kcet_resolver.elf_image import ElfImage
from tools.overlay_to_elf import OverlayElfError, overlay_to_elf


class TestOverlayToElf(unittest.TestCase):
    def test_loads_the_whole_file_at_the_load_address(self):
        raw = overlay(4, 0x100, 0x40, 0x80)
        image = ElfImage(overlay_to_elf(raw))
        self.assertEqual(image.read(LOAD, len(raw)), raw)

    def test_entry_is_the_load_address_and_segment_is_executable(self):
        elf = overlay_to_elf(overlay(4, 0x10, 0, 0))
        self.assertEqual(int.from_bytes(elf[24:28], "little"), LOAD)
        self.assertEqual(int.from_bytes(elf[18:20], "little"), 8)
        flags = int.from_bytes(elf[52 + 24:52 + 28], "little")
        self.assertEqual(flags & 7, 7)

    def test_bss_extends_memory_size_not_file_size(self):
        raw = overlay(4, 0x10, 0x10, 0x200)
        elf = overlay_to_elf(raw)
        filesz = int.from_bytes(elf[52 + 16:52 + 20], "little")
        memsz = int.from_bytes(elf[52 + 20:52 + 24], "little")
        self.assertEqual((filesz, memsz), (len(raw), 0x40 + 0x10 + 0x10 + 0x200))

    def test_code_only_loads_header_and_text_but_keeps_memory_size(self):
        raw = overlay(4, 0x100, 0x40, 0x80)
        elf = overlay_to_elf(raw, code_only=True)
        image = ElfImage(elf)
        self.assertEqual(image.read(LOAD, 0x40 + 0x100), raw[:0x140])
        filesz = int.from_bytes(elf[52 + 16:52 + 20], "little")
        memsz = int.from_bytes(elf[52 + 20:52 + 24], "little")
        self.assertEqual((filesz, memsz), (0x140, 0x40 + 0x100 + 0x40 + 0x80))

    def test_code_only_can_extend_the_text_into_the_data_section(self):
        raw = overlay(4, 0x100, 0x40, 0x80)
        elf = overlay_to_elf(raw, code_only=True, text_bytes=0x140, entry=LOAD + 0x120)
        self.assertEqual(int.from_bytes(elf[52 + 16:52 + 20], "little"), 0x40 + 0x140)

    def test_text_override_must_fit_the_file_and_be_aligned(self):
        raw = overlay(4, 0x100, 0x40, 0x80)
        with self.assertRaises(OverlayElfError):
            overlay_to_elf(raw, code_only=True, text_bytes=-4)
        with self.assertRaises(OverlayElfError):
            overlay_to_elf(raw, code_only=True, text_bytes=len(raw) - 0x40 + 4)
        with self.assertRaises(OverlayElfError):
            overlay_to_elf(raw, code_only=True, text_bytes=0x142)
        self.assertEqual(len(overlay_to_elf(raw, code_only=True, text_bytes=len(raw) - 0x40)), 0x1000 + len(raw))

    def test_entry_can_be_overridden_to_a_function_start(self):
        elf = overlay_to_elf(overlay(4, 0x100, 0, 0), entry=LOAD + 0x80)
        self.assertEqual(int.from_bytes(elf[24:28], "little"), LOAD + 0x80)
        with self.assertRaises(OverlayElfError):
            overlay_to_elf(overlay(4, 0x100, 0, 0), entry=LOAD + 0x80000)
        with self.assertRaises(OverlayElfError):
            overlay_to_elf(overlay(4, 0x100, 0, 0), entry=LOAD + 2)

    def test_rejects_non_overlay_input(self):
        with self.assertRaises(OverlayElfError):
            overlay_to_elf(b"\x7fELF" + b"\0" * 100)


if __name__ == "__main__":
    unittest.main()
