#!/usr/bin/env python3
"""Tests for the desktop run validation driver."""
import subprocess
import tempfile
import unittest
from contextlib import ExitStack
from pathlib import Path
from unittest.mock import Mock, call, patch

from tools import validate_desktop as validation
from tools.classify_diagnostics import RunVerdict
from tools.frame_analysis import FrameKind, FrameSeries


def tick_log(pcs: list) -> str:
    return "".join(f"[run:tick] tick={index} pc=0x{pc:08x}\n" for index, pc in enumerate(pcs))


PROGRESSING_LOG = "Starting execution\n" + tick_log([0x100008 + 0x40 * index for index in range(6)])
REAL_AND_CHANGED = FrameSeries(kinds=(FrameKind.REAL_FRAME, FrameKind.REAL_FRAME), real_frames=2, changed=True)
ONLY_FALLBACK = FrameSeries(kinds=(FrameKind.SOLID_FALLBACK,) * 3, real_frames=0, changed=False)


class TestDesktopValidationPaths(unittest.TestCase):
    def test_screenshot_dir_points_to_pc_subfolder(self):
        self.assertEqual(validation.SCREENSHOT_DIR.name, "pc")
        self.assertEqual(validation.SCREENSHOT_DIR.parent.name, ".playwright-mcp")

    def test_capture_screenshot_adds_date_fingerprint(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            with patch.object(validation, "SCREENSHOT_DIR", tmp_path), \
                 patch("subprocess.run") as mock_run:
                mock_run.return_value = Mock(stdout=b"fake-xwd")
                dest = validation.capture_screenshot("campaign-auto-map-submitted.png")
                # Filename must have date fingerprint YYYYMMDD-HHMMSS
                import re
                self.assertTrue(re.search(r"campaign-auto-map-submitted-\d{8}-\d{6}(?:-\d+)?\.png$", dest.name))
                # Canonical symlink/file must point to or match the fingerprinted file
                canonical = tmp_path / "campaign-auto-map-submitted.png"
                self.assertTrue(canonical.exists() or canonical.is_symlink())

    def test_capture_screenshot_does_not_duplicate_fingerprint(self):
        with tempfile.TemporaryDirectory() as tmpdir:
            tmp_path = Path(tmpdir)
            with patch.object(validation, "SCREENSHOT_DIR", tmp_path), \
                 patch("subprocess.run") as mock_run:
                mock_run.return_value = Mock(stdout=b"fake-xwd")
                already_stamped = "test-screen-20261030-193000.png"
                dest = validation.capture_screenshot(already_stamped)
                self.assertEqual(dest.name, already_stamped)


class TestDesktopValidation(unittest.TestCase):
    def setUp(self):
        self.stack = ExitStack()
        self.addCleanup(self.stack.close)
        directory = self.stack.enter_context(tempfile.TemporaryDirectory())
        self.evidence = Path(directory)
        self.stack.enter_context(patch.object(validation, "EVIDENCE_DIR", self.evidence))
        self.stack.enter_context(patch.object(validation, "SCREENSHOT_DIR", self.evidence))
        self.stack.enter_context(patch.object(validation.time, "sleep"))
        self.capture = self.stack.enter_context(patch.object(validation, "capture_screenshot", return_value=Path("shot.png")))
        self.popen = self.stack.enter_context(patch.object(validation.subprocess, "Popen"))

    def test_shutdown_escalates_and_preserves_completed_run(self):
        process = self.popen.return_value
        process.poll.return_value = None
        process.wait.side_effect = [subprocess.TimeoutExpired("game", 10), -9]
        self.assertEqual(validation.run_game(1, 1), (True, ["shot.png"]))
        self.assertEqual(process.method_calls, [call.poll(), call.terminate(),
                                              call.wait(timeout=10), call.kill(), call.wait(timeout=10)])

    def test_early_exit_remains_failed(self):
        process = self.popen.return_value
        process.poll.return_value = 1
        self.assertEqual(validation.run_game(1, 1), (False, ["shot.png"]))
        process.kill.assert_not_called()

    def test_capture_error_cleans_up_game_and_propagates(self):
        error = RuntimeError("capture failed")
        self.capture.side_effect = error
        with self.assertRaises(RuntimeError) as caught:
            validation.run_game(1, 1)
        self.assertIs(caught.exception, error)
        self.popen.return_value.terminate.assert_called_once_with()
        self.popen.return_value.wait.assert_called_once_with(timeout=10)

    def test_kill_reap_timeout_is_bounded_and_propagates(self):
        process = self.popen.return_value
        process.poll.return_value = None
        error = subprocess.TimeoutExpired("game", 10)
        process.wait.side_effect = [subprocess.TimeoutExpired("game", 10), error]
        with self.assertRaises(subprocess.TimeoutExpired) as caught:
            validation.run_game(1, 1)
        self.assertIs(caught.exception, error)
        process.kill.assert_called_once_with()
        self.assertEqual(process.wait.call_args_list, [call(timeout=10), call(timeout=10)])

    def test_main_classifies_after_game_and_display_escalation(self):
        display = Mock()
        game = Mock()
        game.poll.return_value = None
        for process in (display, game):
            process.wait.side_effect = [subprocess.TimeoutExpired("child", 10), -9]
        self.popen.side_effect = [display, game]

        def capture(name):
            (self.evidence / "run.log").write_text(PROGRESSING_LOG)
            return Path(name)

        self.capture.side_effect = capture
        with patch.object(validation, "check_preflight"), \
                patch.object(validation, "analyze_frames", return_value=REAL_AND_CHANGED), \
                patch("builtins.print") as output:
            self.assertEqual(validation.main(["--run-seconds", "1", "--interval", "1"]), validation.EXIT_PASS)
        self.assertIn("running_at_end=True, guest=progressing", output.call_args.args[0])
        for process in (display, game):
            process.kill.assert_called_once_with()
            self.assertEqual(process.wait.call_args_list, [call(timeout=10), call(timeout=10)])

    def test_main_reaps_display_when_game_launch_fails(self):
        error = OSError("launch failed")
        with patch.object(validation, "check_preflight"), \
                patch.object(validation, "run_game", side_effect=error):
            with self.assertRaises(OSError) as caught:
                validation.main([])
        self.assertIs(caught.exception, error)
        self.popen.return_value.terminate.assert_called_once_with()
        self.popen.return_value.wait.assert_called_once_with(timeout=10)

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

    def test_container_command_options_and_default_jobs(self):
        self.assertEqual(validation.DEFAULT_JOBS, 8)
        cmd = validation.container_command(run_seconds=30, interval=10, jobs=8)
        self.assertIn("--name=aoe2-validate-desktop", cmd)
        self.assertIn("--hostname=aoe2-validate-desktop", cmd)
        self.assertIn("--label=aoe2.platform=desktop", cmd)
        self.assertIn("--label=aoe2.role=validate", cmd)
        self.assertIn("--cpus=8", cmd)
        self.assertIn("aoe2-build-desktop", cmd)


if __name__ == "__main__":
    unittest.main()
