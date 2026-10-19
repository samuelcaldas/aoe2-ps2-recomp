import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path
import subprocess
import sys

REPO_ROOT = Path(__file__).resolve().parent.parent

class TestDependenciesBootstrap(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.test_root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_check_mode_without_writes(self):
        lock_file = self.test_root / "deps.lock.json"
        third_party_dir = self.test_root / "third_party"
        
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "bootstrap_dependencies.py"),
            "--check",
            "--lock-file",
            str(lock_file),
            "--dest-dir",
            str(third_party_dir),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("missing", res.stdout.lower() + res.stderr.lower())
        self.assertFalse(third_party_dir.exists())
        self.assertFalse(lock_file.exists())
