#!/usr/bin/env python3
"""Tests for the Nintendo Switch emulator validation driver."""
import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from tools import validate_switch as validation


def histogram(counts: dict) -> str:
    return "\n".join(f"{count:>10}: ({r},{g},{b}) #000000 x" for (r, g, b), count in counts.items())


class FakeImageMagick:
    """Answers the histogram and compare calls that the frame analyzer makes."""

    def __init__(self, frame_histograms: dict, metric: str = "0"):
        self.frame_histograms = frame_histograms
        self.metric = metric

    def __call__(self, args):
        if args[1] == "compare":
            return self.metric
        return self.frame_histograms[Path(args[1]).name]


REAL_COLORS = {(0, 0, 0): 1000, **{(index * 3, 90, 200): 50 for index in range(40)}}
FALLBACK_COLORS = {(0, 0, 0): 1000, (255, 0, 255): 60000}
PROGRESSING_LOG = "".join(f"ps2x: [run:tick] tick={i} pc=0x{0x100008 + 0x40 * i:08x}\n" for i in range(6))


class TestSwitchValidationScript(unittest.TestCase):
    def setUp(self):
        self.script = validation.emulator_script(run_seconds=45)

    def test_starts_virtual_display_and_headless_emulator(self):
        self.assertIn("Xvfb :99", self.script)
        self.assertIn("DISPLAY=:99", self.script)
        self.assertIn("Ryujinx.Headless.SDL2", self.script)
        self.assertIn("--root-data-dir", self.script)

    def test_copies_keys_if_provided(self):
        self.assertIn("prod.keys", self.script)

    def test_collects_screenshots_and_checks_liveness(self):
        self.assertIn("xwd -root", self.script)
        self.assertIn("alive.txt", self.script)
        self.assertIn("run.log", self.script)

    def test_container_command_options(self):
        command = validation.container_command(run_seconds=60, jobs=4)
        self.assertIn("--memory=12g", command)
        self.assertIn("--cpus=4", command)
        self.assertIn("aoe2-switch-emulator", command)

    def test_verdict_passes_only_with_progress_and_changing_real_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [str(Path(directory) / f"screen-{index}.png") for index in (1, 2)]
            magick = FakeImageMagick(
                {"screen-1.png": histogram(REAL_COLORS), "screen-2.png": histogram(REAL_COLORS)},
                metric="5000",
            )
            self.assertTrue(validation.evidence_passed(True, PROGRESSING_LOG, shots, magick))
            self.assertFalse(validation.evidence_passed(False, PROGRESSING_LOG, shots, magick))
            self.assertFalse(validation.evidence_passed(True, "", shots, magick))

    def test_fallback_screenshots_fail_even_when_the_process_is_alive(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [str(Path(directory) / f"screen-{index}.png") for index in (1, 2)]
            magick = FakeImageMagick(
                {"screen-1.png": histogram(FALLBACK_COLORS), "screen-2.png": histogram(FALLBACK_COLORS)}
            )
            self.assertFalse(validation.evidence_passed(True, PROGRESSING_LOG, shots, magick))

    def test_preflight_fails_fast_on_missing_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            fake_keys = Path(directory) / "prod.keys"
            with patch.object(validation, "NRO_PATH", "nonexistent/aoe2_game.nro"):
                with self.assertRaises(validation.PreflightError):
                    validation.check_preflight(keys_path=fake_keys)

    def test_preflight_allows_missing_keys_for_homebrew(self):
        with tempfile.TemporaryDirectory() as directory:
            fake_keys = Path(directory) / "nonexistent.keys"
            with patch.object(validation, "NRO_PATH", "tools/validate_switch.py"):
                # Should not raise PreflightError when binary exists, even if keys are absent
                validation.check_preflight(keys_path=fake_keys)


if __name__ == "__main__":
    unittest.main()
