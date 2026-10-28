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

    def create_repository(self, name):
        repository = self.test_root / "third_party" / name
        repository.mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(repository)], check=True)
        subprocess.run(["git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                        "-c", "commit.gpgsign=false", "commit", "--allow-empty", "-qm", "fixture"],
                       cwd=repository, check=True)
        return subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repository, text=True).strip()

    def run_bootstrap(self, *arguments):
        return subprocess.run([sys.executable, str(REPO_ROOT / "tools/bootstrap_dependencies.py"),
                               "--lock-file", str(self.test_root / "deps.lock.json"),
                               "--dest-dir", str(self.test_root / "third_party"), *arguments],
                              capture_output=True, text=True)

    def test_record_current_preserves_other_dependencies(self):
        commit = self.create_repository("PS2Recomp")
        data = {"version": 1, "dependencies": {
            "PS2Recomp": {"commit_sha": "0" * 40, "url": "https://github.com/samuelcaldas/PS2Recomp.git"},
            "other": {"commit_sha": "1" * 40, "notes": "preserve"}}}
        lock = self.test_root / "deps.lock.json"
        lock.write_text(json.dumps(data), encoding="utf-8")
        result = self.run_bootstrap("--record-current", "PS2Recomp")
        self.assertEqual(result.returncode, 0, result.stderr)
        recorded = json.loads(lock.read_text(encoding="utf-8"))
        self.assertEqual(recorded["dependencies"]["PS2Recomp"]["commit_sha"], commit)
        self.assertEqual(recorded["dependencies"]["other"], data["dependencies"]["other"])

    def test_check_rejects_commit_different_from_lockfile(self):
        dependencies = {}
        for name in ("PS2Recomp", "libnx", "deko3d"):
            self.create_repository(name)
            dependencies[name] = {"commit_sha": "0" * 40}
        lock = self.test_root / "deps.lock.json"
        lock.write_text(json.dumps({"dependencies": dependencies}), encoding="utf-8")
        before = lock.read_bytes()
        result = self.run_bootstrap("--check")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("commit mismatch", result.stderr.lower())
        self.assertEqual(lock.read_bytes(), before)

    def test_bootstrap_reproduces_locked_commits_without_dropping_other_entries(self):
        dependencies = {}
        for name in ("PS2Recomp", "libnx", "deko3d"):
            commit = self.create_repository(name)
            dependencies[name] = {"commit_sha": commit,
                                  "url": str(self.test_root / "third_party" / name)}
        dependencies["other"] = {"commit_sha": "1" * 40}
        lock = self.test_root / "deps.lock.json"
        lock.write_text(json.dumps({"version": 1, "dependencies": dependencies}), encoding="utf-8")
        repository = self.test_root / "third_party" / "PS2Recomp"
        subprocess.run(["git", "-c", "user.name=Test", "-c", "user.email=test@example.invalid",
                        "-c", "commit.gpgsign=false", "commit", "--allow-empty", "-qm", "new HEAD"],
                       cwd=repository, check=True)
        result = self.run_bootstrap()
        self.assertEqual(result.returncode, 0, result.stderr)
        recorded = json.loads(lock.read_text(encoding="utf-8"))
        self.assertEqual(recorded["dependencies"]["other"], dependencies["other"])
        for name in ("PS2Recomp", "libnx", "deko3d"):
            actual = subprocess.check_output(["git", "rev-parse", "HEAD"],
                                             cwd=self.test_root / "third_party" / name, text=True).strip()
            self.assertEqual(actual, dependencies[name]["commit_sha"])

    def test_fork_configuration_has_no_patch_series(self):
        from tools.bootstrap_dependencies import DEPENDENCIES, PATCH_DIRECTORIES
        self.assertEqual(DEPENDENCIES["PS2Recomp"]["url"],
                         "https://github.com/samuelcaldas/PS2Recomp.git")
        self.assertNotIn("PS2Recomp", PATCH_DIRECTORIES)

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
