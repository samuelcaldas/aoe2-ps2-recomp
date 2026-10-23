import unittest

from tools.run_overlay_pipeline import PipelineError, first_function_start, rewrite_toml

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


if __name__ == "__main__":
    unittest.main()
