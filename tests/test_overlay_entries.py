import unittest

from tests.test_kcet_resolver_elf import PF_RX, build_elf, code
from tools.kcet_resolver.elf_image import ElfImage
from tools.kcet_resolver.mips import jal
from tools.overlay_entries import entry_targets, seeds_for

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


if __name__ == "__main__":
    unittest.main()
