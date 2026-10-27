#!/usr/bin/env python3
"""Tests for the Ghidra command composed by the PS2Recomp pipeline."""
import unittest
from pathlib import Path

from tools import run_ps2recomp_pipeline as pipeline


class TestGhidraCommand(unittest.TestCase):
    def setUp(self):
        self.command = pipeline.build_ghidra_command(
            analyze_headless=Path("/ghidra/support/analyzeHeadless"),
            project_dir=Path("/repo/analysis/ghidra"),
            elf_path=Path("/repo/game/AOK.ELF"),
            exporter_script=Path("/repo/third_party/exporter/ExportPS2Functions.java"),
            toml_config=Path("/repo/analysis/generated/SLES-50282.toml"),
            csv_map=Path("/repo/analysis/generated/function_map.csv"),
        )

    def post_scripts(self):
        return [self.command[index + 1] for index, token in enumerate(self.command) if token == "-postScript"]

    def test_data_regions_are_marked_before_export(self):
        self.assertEqual(self.post_scripts(), ["TrimDisjointFunctionBodies.java", "MarkDataRegions.java",
                                               "DiscoverReferencedFunctions.java", "ExportPS2Functions.java"])

    def test_data_region_threshold_matches_guardrail(self):
        mark_index = self.command.index("MarkDataRegions.java")
        expected = str(pipeline.function_map_guardrail.DEFAULT_MAX_RETURN_GAP_BYTES)
        self.assertEqual(self.command[mark_index + 1], expected)

    def test_imports_with_emotion_engine_language(self):
        processor = self.command[self.command.index("-processor") + 1]
        self.assertEqual(processor, pipeline.ee_extension.LANGUAGE_ID)

    def test_script_path_includes_local_and_exporter_directories(self):
        script_path = self.command[self.command.index("-scriptPath") + 1].split(";")
        self.assertIn(str(pipeline.DATA_REGION_SCRIPT.parent), script_path)
        self.assertIn("/repo/third_party/exporter", script_path)


if __name__ == "__main__":
    unittest.main()
