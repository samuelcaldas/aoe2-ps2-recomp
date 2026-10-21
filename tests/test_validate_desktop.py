#!/usr/bin/env python3
"""Tests for the desktop run validation driver."""
import tempfile
import unittest
from pathlib import Path

from tools import validate_desktop as validation
from tools.classify_diagnostics import RunVerdict
from tools.frame_analysis import FrameKind, FrameSeries


def tick_log(pcs: list) -> str:
    return "".join(f"[run:tick] tick={index} pc=0x{pc:08x}\n" for index, pc in enumerate(pcs))


PROGRESSING_LOG = "Starting execution\n" + tick_log([0x100008 + 0x40 * index for index in range(6)])
REAL_AND_CHANGED = FrameSeries(kinds=(FrameKind.REAL_FRAME, FrameKind.REAL_FRAME), real_frames=2, changed=True)
ONLY_FALLBACK = FrameSeries(kinds=(FrameKind.SOLID_FALLBACK,) * 3, real_frames=0, changed=False)


class TestDesktopValidation(unittest.TestCase):
    def test_game_command_boots_owned_elf(self):
        command = validation.game_command()
        self.assertTrue(command[0].endswith("aoe2_game"))
        self.assertTrue(command[1].endswith("game/SLES-50282/AOK.ELF"))

    def test_game_command_passes_owned_disc_image_when_present(self):
        with tempfile.TemporaryDirectory() as directory:
            image = Path(directory) / "aoe2.iso"
            image.write_bytes(b"")
            command = validation.game_command(image)
        self.assertEqual(command[2], str(image))

    def test_game_command_omits_missing_disc_image(self):
        command = validation.game_command(Path("/nonexistent/aoe2.iso"))
        self.assertEqual(len(command), 2)

    def test_game_comes_from_the_diagnostic_build(self):
        self.assertIn("docker-desktop-diagnostics", str(validation.GAME_BINARY))

    def test_summary_counts_missing_branch_targets(self):
        log = "Starting execution\n[guest-branch:missing-target] a\n[guest-branch:missing-target] b\n"
        summary = validation.summarize_log(log)
        self.assertTrue(summary.started)
        self.assertEqual(summary.missing_branch_targets, 2)

    def test_summary_carries_the_run_verdict(self):
        self.assertIs(validation.summarize_log(PROGRESSING_LOG).verdict, RunVerdict.PROGRESSING)
        self.assertIs(validation.summarize_log("Starting execution\n").verdict, RunVerdict.NO_EVIDENCE)

    def test_pass_requires_start_survival_progress_and_changing_real_frames(self):
        summary = validation.summarize_log(PROGRESSING_LOG)
        self.assertTrue(validation.passed(summary, still_running=True, frames=REAL_AND_CHANGED))
        self.assertFalse(validation.passed(summary, still_running=False, frames=REAL_AND_CHANGED))
        self.assertFalse(validation.passed(summary, still_running=True, frames=ONLY_FALLBACK))

    def test_pass_fails_without_tick_evidence(self):
        summary = validation.summarize_log("Starting execution\n")
        self.assertFalse(validation.passed(summary, still_running=True, frames=REAL_AND_CHANGED))

    def test_pass_fails_when_guest_is_stalled_even_with_real_frames(self):
        stalled = "Starting execution\n" + tick_log([0x200000, 0x200004] * 4)
        summary = validation.summarize_log(stalled)
        self.assertFalse(validation.passed(summary, still_running=True, frames=REAL_AND_CHANGED))


if __name__ == "__main__":
    unittest.main()
