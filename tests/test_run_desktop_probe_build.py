#!/usr/bin/env python3
"""Tests for the resource-guarded desktop probe build runner."""
import unittest
from unittest import mock

from tools import run_desktop_probe_build as runner


class TestRunDesktopProbeBuild(unittest.TestCase):
    def test_command_confines_memory_and_limits_jobs(self):
        args = runner.parse_args(["--jobs", "3"])
        command = runner.build_command(args)
        self.assertIn("MemorySwapMax=0", command)
        self.assertIn("MemoryMax=18G", command)
        self.assertIn("-j3", command)
        self.assertIn("-l3", command)
        self.assertIn("--scope", command)

    def test_build_environment_forces_english_diagnostics(self):
        environment = runner.build_environment()
        self.assertEqual(environment["LC_ALL"], "C")
        self.assertIn("PATH", environment)

    def test_preflight_fails_when_not_enough_memory(self):
        args = runner.parse_args(["--min-available-gib", "1000"])
        with self.assertRaises(runner.PreflightError):
            runner.check_preflight(args)

    def test_preflight_fails_when_build_already_running(self):
        args = runner.parse_args([])
        with mock.patch.object(runner, "running_build_processes", return_value=["ninja"]):
            with self.assertRaises(runner.PreflightError):
                runner.check_preflight(args)

    def test_main_returns_preflight_code_on_failure(self):
        with mock.patch.object(runner, "running_build_processes", return_value=["cc1plus"]):
            self.assertEqual(runner.main(["--dry-run"]), runner.EXIT_PREFLIGHT)

    def test_digest_prints_only_every_hundredth_progress_line(self):
        with mock.patch("builtins.print") as printed:
            runner.digest_line("[101/200] Building x\n", "")
            runner.digest_line("[200/200] Building y\n", "")
        self.assertEqual(printed.call_count, 1)


if __name__ == "__main__":
    unittest.main()
