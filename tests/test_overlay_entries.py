import unittest

from tests.test_kcet_resolver_elf import PF_RX, build_elf, code
from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.mips import jal
from tools.overlay_entries import entry_targets, seeds_for, switch_targets

BASE = 0x100000
J = 0x08000000


def jump(target):
    return J | ((target >> 2) & 0x3FFFFFF)


def image(*words):
    data = code(*words)
    return ElfImage(build_elf([(BASE, data, len(data), PF_RX)]))


class TestEntryTargets(unittest.TestCase):
    def test_collects_call_and_jump_targets_inside_the_ranges(self):
        found = entry_targets(image(jal(0x1D98080), jump(0x1D98180), jal(0x200000), jal(0x1D98080)),
                              [(0x1D98000, 0x2000000)])
        self.assertEqual(found, [0x1D98080, 0x1D98180])

    def test_ignores_targets_outside_all_ranges(self):
        self.assertEqual(entry_targets(image(jal(0x100000)), [(0x1D98000, 0x2000000)]), [])

    def test_seeds_are_limited_to_the_overlay_text_and_sorted(self):
        targets = [0x1D98080, 0x1D98630, 0x1D99000]
        self.assertEqual(seeds_for(targets, 0x1D98000, 0x40, 0x500), [0x1D98080])
        self.assertEqual(seeds_for(targets, 0x1D98000, 0x40, 0x2000), [0x1D98080, 0x1D98630, 0x1D99000])

    def test_seed_in_the_header_is_excluded(self):
        self.assertEqual(seeds_for([0x1D98010], 0x1D98000, 0x40, 0x600), [])


LUI_V0, ADDIU_V1_V0 = 0x3C020000, 0x24430000


class TestSwitchTargets(unittest.TestCase):
    LOAD, HEADER, TEXT = 0x1D98000, 0x40, 0x80

    def overlay(self, table):
        """Text builds the table address (data at the end of the text) then the table follows."""
        table_address = self.LOAD + self.HEADER + self.TEXT
        text = [0] * (self.HEADER // 4) + [LUI_V0 | (((table_address + 0x8000) >> 16) & 0xFFFF), ADDIU_V1_V0 | (table_address & 0xFFFF)]
        text += [0] * ((self.HEADER + self.TEXT) // 4 - len(text))
        return b"".join(word.to_bytes(4, "little") for word in text + table)

    def test_collects_every_entry_of_a_referenced_table(self):
        inside = [self.LOAD + 0x60, self.LOAD + 0x64, self.LOAD + 0x70]
        raw = self.overlay(inside + [0])
        self.assertEqual(switch_targets(raw, self.LOAD, self.HEADER, self.TEXT), inside)

    def test_ignores_unreferenced_runs_of_addresses(self):
        empty_text = bytes(self.HEADER + self.TEXT)
        table = b"".join(w.to_bytes(4, "little") for w in [self.LOAD + 0x60, self.LOAD + 0x64])
        self.assertEqual(switch_targets(empty_text + table, self.LOAD, self.HEADER, self.TEXT), [])

    def test_a_single_stray_address_is_not_a_table(self):
        self.assertEqual(switch_targets(self.overlay([self.LOAD + 0x60, 0]), self.LOAD, self.HEADER, self.TEXT), [])


if __name__ == "__main__":
    unittest.main()
