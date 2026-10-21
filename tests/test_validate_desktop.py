#!/usr/bin/env python3
"""Tests for the desktop run validation driver."""
import unittest

from tools import validate_desktop as validation


class TestDesktopValidation(unittest.TestCase):
    def test_game_command_boots_owned_elf(self):
        command = validation.game_command()
        self.assertTrue(command[0].endswith("aoe2_game"))
        self.assertTrue(command[1].endswith("game/SLES-50282/AOK.ELF"))

    def test_summary_counts_missing_branch_targets(self):
        log = "Starting execution\n[guest-branch:missing-target] a\n[guest-branch:missing-target] b\n"
        summary = validation.summarize_log(log)
        self.assertTrue(summary.started)
        self.assertEqual(summary.missing_branch_targets, 2)

    def test_verdict_requires_start_and_survival(self):
        started = validation.summarize_log("Starting execution\n")
        self.assertTrue(validation.passed(started, still_running=True))
        self.assertFalse(validation.passed(started, still_running=False))
        self.assertFalse(validation.passed(validation.summarize_log(""), still_running=True))


if __name__ == "__main__":
    unittest.main()
