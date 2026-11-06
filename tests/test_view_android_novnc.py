#!/usr/bin/env python3
"""Tests for the interactive Android noVNC runner (tools/view_android_novnc.py)."""
from __future__ import annotations

import unittest
from pathlib import Path
from unittest import mock

from tools import view_android_novnc as view_android


class TestViewAndroidNoVnc(unittest.TestCase):
    def test_novnc_runner_script_contains_emulator_display_and_novnc_setup(self):
        script = view_android.novnc_runner_script("build/docker-android/aoe2-recomp-debug.apk")
        self.assertIn("Xvfb :99 -screen 0 1280x760x24", script)
        self.assertIn("export DISPLAY=:99", script)
        self.assertIn("emulator -avd aoe2 -gpu swiftshader_indirect", script)
        self.assertIn("x11vnc -display :99", script)
        self.assertIn("websockify --web", script)
        self.assertIn("build/docker-android/aoe2-recomp-debug.apk", script)
        self.assertIn("adb shell am start -n", script)
        self.assertIn("manual-run.log", script)

    def test_main_dry_run_constructs_docker_command_with_kvm_and_ports(self):
        with mock.patch("tools.validate_android.check_preflight"):
            with mock.patch("builtins.print") as mock_print:
                ret = view_android.main(["--dry-run", "--port", "6090", "--vnc-port", "5910"])
                self.assertEqual(ret, 0)
                cmd_str = "".join(call[0][0] for call in mock_print.call_args_list if call[0])
                self.assertIn("--name=aoe2-view-android", cmd_str)
                self.assertIn("--label=aoe2.platform=android", cmd_str)
                self.assertIn("--label=aoe2.role=view", cmd_str)
                self.assertIn("--device /dev/kvm", cmd_str)
                self.assertIn("-p 6090:6082", cmd_str)
                self.assertIn("-p 5910:5900", cmd_str)
                self.assertIn("aoe2-build-android", cmd_str)


if __name__ == "__main__":
    unittest.main()
