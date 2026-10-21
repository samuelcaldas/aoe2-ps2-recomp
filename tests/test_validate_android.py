#!/usr/bin/env python3
"""Tests for the Android emulator validation driver."""
import tempfile
import unittest
from pathlib import Path

from tools import validate_android as validation


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
PROGRESSING_LOGCAT = "".join(f"ps2x: [run:tick] tick={i} pc=0x{0x100008 + 0x40 * i:08x}\n" for i in range(6))


class TestAndroidValidationScript(unittest.TestCase):
    def setUp(self):
        self.script = validation.emulator_script(run_seconds=45)

    def test_boots_headless_x86_64_emulator_with_kvm(self):
        self.assertIn("system-images;android-34;default;x86_64", self.script)
        self.assertIn("-no-window", self.script)
        self.assertIn("sys.boot_completed", self.script)

    def test_installs_apk_prepares_nested_game_data_and_launches(self):
        self.assertIn("adb install -r", self.script)
        self.assertIn(validation.DEVICE_GAME_DIR, self.script)
        self.assertIn("adb push", self.script)
        self.assertIn("kcet_hooks.ini", self.script)
        self.assertIn("android.app.NativeActivity", self.script)

    def test_collects_timed_screenshots_logcat_and_liveness(self):
        self.assertIn("for shot in", self.script)
        self.assertIn("sleep 15", self.script)
        self.assertIn("screencap", self.script)
        self.assertIn("pidof", self.script)
        self.assertIn("logcat -d", self.script)

    def test_container_gets_kvm_device_and_group(self):
        command = validation.container_command(run_seconds=45)
        self.assertIn("--device", command)
        self.assertIn("/dev/kvm", command)
        group_index = command.index("--group-add") + 1
        self.assertEqual(command[group_index], str(validation.os.stat("/dev/kvm").st_gid))

    def test_verdict_passes_only_with_progress_and_changing_real_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [str(Path(directory) / f"screen-{index}.png") for index in (1, 2)]
            magick = FakeImageMagick({"screen-1.png": histogram(REAL_COLORS), "screen-2.png": histogram(REAL_COLORS)},
                                     metric="5000")
            self.assertTrue(validation.evidence_passed(True, PROGRESSING_LOGCAT, shots, magick))
            self.assertFalse(validation.evidence_passed(False, PROGRESSING_LOGCAT, shots, magick))
            self.assertFalse(validation.evidence_passed(True, "", shots, magick))

    def test_fallback_screenshots_fail_even_when_the_process_is_alive(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [str(Path(directory) / f"screen-{index}.png") for index in (1, 2)]
            magick = FakeImageMagick({"screen-1.png": histogram(FALLBACK_COLORS), "screen-2.png": histogram(FALLBACK_COLORS)})
            self.assertFalse(validation.evidence_passed(True, PROGRESSING_LOGCAT, shots, magick))


if __name__ == "__main__":
    unittest.main()
