#!/usr/bin/env python3
"""Tests for the Android emulator validation driver."""
import unittest

from tools import validate_android as validation


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

    def test_collects_evidence_and_checks_process(self):
        self.assertIn("sleep 45", self.script)
        self.assertIn("screencap", self.script)
        self.assertIn("pidof", self.script)
        self.assertIn("logcat -d", self.script)

    def test_container_gets_kvm_device_and_group(self):
        command = validation.container_command(run_seconds=45)
        self.assertIn("--device", command)
        self.assertIn("/dev/kvm", command)
        group_index = command.index("--group-add") + 1
        self.assertEqual(command[group_index], str(validation.os.stat("/dev/kvm").st_gid))


if __name__ == "__main__":
    unittest.main()
