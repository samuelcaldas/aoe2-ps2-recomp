import unittest

from tests.test_map_overlays import LOAD, overlay
from tools.map_overlays import HEADER_BYTES, parse_overlay_header
from tools.run_overlay_pipeline import (
    PipelineError, entry_range, first_function_start, overlay_seeds, rewrite_toml,
)

JR_RA = 0x03E00008

CSV = """Name,Start,End,Size
entry_01d98000,0x01D98000,0x01D98010,16
FUN_01d984b0,0x01D984B0,0x01D984B4,4
FUN_01d98630,0x01D98630,0x01D98654,36
"""
TOML = """[general]
input = "/x/MULTIMCD.BIN"
output = "/x/out"
ghidra_output = "/x/m.csv"
"""


class TestPipelineHelpers(unittest.TestCase):
    def test_entry_is_the_first_function_not_a_continuation_label(self):
        self.assertEqual(first_function_start(CSV), 0x1D984B0)

    def test_no_function_is_an_error(self):
        with self.assertRaises(PipelineError):
            first_function_start("Name,Start,End,Size\nentry_x,0x1,0x2,1\n")

    def test_toml_paths_are_rewritten_and_others_kept(self):
        text = rewrite_toml(TOML, "a.elf", "gen/dir")
        self.assertIn('input = "a.elf"', text)
        self.assertIn('output = "gen/dir"', text)
        self.assertIn('ghidra_output = "/x/m.csv"', text)

    def test_toml_without_input_or_output_is_an_error(self):
        with self.assertRaises(PipelineError):
            rewrite_toml("[general]\n", "a", "b")


class TestCodeThroughData(unittest.TestCase):
    def test_entry_range_covers_the_data_section_only_for_mo_ov(self):
        header = parse_overlay_header(overlay(2, 0xC0, 0x154, 0))
        self.assertEqual(entry_range({"file": "MO_OV_CD.BIN"}, header),
                         (LOAD + HEADER_BYTES, LOAD + HEADER_BYTES + 0xC0 + 0x154))
        self.assertEqual(entry_range({"file": "SMENU_CD.BIN"}, header),
                         (LOAD + HEADER_BYTES, LOAD + HEADER_BYTES + 0xC0))


class TestOverlaySeeds(unittest.TestCase):
    LOAD, HEADER, TEXT = 0x1D98000, 0x40, 0x80

    def overlay_with_switch(self):
        """Text: a lui/addiu pair building a table at the text end, and a jr $ra at 0x5C.

        The table lists cases 0x60 (delay slot of the jr) and 0x70 (plain instruction)."""
        table_address = self.LOAD + self.HEADER + self.TEXT
        text = [0] * (self.TEXT // 4)
        text[0] = 0x3C020000 | (((table_address + 0x8000) >> 16) & 0xFFFF)
        text[1] = 0x24430000 | (table_address & 0xFFFF)
        text[(0x5C - self.HEADER) // 4] = JR_RA
        table = [self.LOAD + 0x60, self.LOAD + 0x70, 0]
        body = b"".join(word.to_bytes(4, "little") for word in text + table)
        return overlay(4, self.TEXT, 0, 0)[:self.HEADER] + body

    def test_a_switch_case_in_a_delay_slot_is_not_seeded(self):
        raw = self.overlay_with_switch()
        header = parse_overlay_header(raw)
        self.assertEqual(overlay_seeds(raw, header, [], self.TEXT), [self.LOAD + 0x70])

    def test_the_extension_start_is_seeded_even_after_a_branch(self):
        raw = overlay(2, 0xC0, 0x154, 0)
        header = parse_overlay_header(raw)
        self.assertIn(LOAD + HEADER_BYTES + 0xC0, overlay_seeds(raw, header, [], 0xC0 + 0x154))


if __name__ == "__main__":
    unittest.main()
