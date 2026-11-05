import unittest
from unittest.mock import patch

from tools import validate_desktop, validate_switch


class ValidationBuildTypes(unittest.TestCase):
    def test_desktop_container_forwards_reference_seconds(self):
        command = validate_desktop.container_command(7, 3, build_type="debug")
        self.assertIn("--run-seconds 7 --interval 3 --build-type debug", command[-1])

    def test_switch_scales_outer_limit_once_and_passes_reference(self):
        for build, shots in (("release", 6), ("debug", 60)):
            script = validate_switch.emulator_script(60, build_type=build)
            self.assertIn(f"seq 1 {shots}", script)
            self.assertIn(f"--run-seconds 60 --build-type {build}", script)
            self.assertIn("wait $DRIVER_PID || DRIVER_STATUS=$?", script)
            self.assertIn("exit $DRIVER_STATUS", script)

    def test_desktop_scales_explicit_duration_once(self):
        with patch.object(validate_desktop, "check_preflight"), \
             patch.object(validate_desktop, "run_game", return_value=(False, [])) as run, \
             patch.object(validate_desktop.subprocess, "Popen"), \
             patch.object(validate_desktop, "stop_process"), \
             patch.object(validate_desktop.time, "sleep"), \
             patch.object(validate_desktop, "analyze_frames") as frames, \
             patch.object(validate_desktop, "summarize_log") as summary, \
             patch.object(validate_desktop, "passed", return_value=False), \
             patch.object(validate_desktop.Path, "read_text", return_value=""):
            frames.return_value.real_frames = 0
            frames.return_value.changed = False
            summary.return_value.verdict.value = "no_evidence"
            validate_desktop.main(["--build-type", "debug", "--run-seconds", "7"])
            run.assert_called_once_with(70, 15)
