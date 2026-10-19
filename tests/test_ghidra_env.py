import os
import unittest
from pathlib import Path
import subprocess
import sys

REPO_ROOT = Path(__file__).resolve().parent.parent

class TestGhidraWorkflow(unittest.TestCase):
    def test_ghidra_home_validation(self):
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "validate_ghidra_env.py"),
        ]
        # When GHIDRA_HOME is unset
        env = os.environ.copy()
        env.pop("GHIDRA_HOME", None)
        res = subprocess.run(cmd, capture_output=True, text=True, env=env)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("ghidra_home", res.stderr.lower() + res.stdout.lower())
