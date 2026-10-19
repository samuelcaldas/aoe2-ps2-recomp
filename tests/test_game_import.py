import hashlib
import os
import shutil
import tempfile
import unittest
from pathlib import Path
import subprocess
import sys

REPO_ROOT = Path(__file__).resolve().parent.parent

class TestGameImportAndVerification(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.test_root = Path(self.temp_dir.name)
        self.expected_sha1 = "f478e19f2059d52899b691483ef5c7522e30e17c"
        
    def tearDown(self):
        self.temp_dir.cleanup()

    def test_import_missing_source(self):
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "import_owned_game.py"),
            "--source",
            str(self.test_root / "AOK.ELF"),
            "--dest-dir",
            str(self.test_root / "game" / "SLES-50282"),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("not found", res.stderr.lower() + res.stdout.lower())

    def test_import_wrong_filename(self):
        dummy_file = self.test_root / "wrong_name.ELF"
        dummy_file.write_bytes(b"content")
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "import_owned_game.py"),
            "--source",
            str(dummy_file),
            "--dest-dir",
            str(self.test_root / "game" / "SLES-50282"),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("expected 'aok.elf'", res.stderr.lower() + res.stdout.lower())

    def test_import_wrong_sha1(self):
        dummy_file = self.test_root / "AOK.ELF"
        dummy_file.write_bytes(b"invalid elf content")
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "import_owned_game.py"),
            "--source",
            str(dummy_file),
            "--dest-dir",
            str(self.test_root / "game" / "SLES-50282"),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("checksum mismatch", res.stderr.lower() + res.stdout.lower())

    def test_import_unexpected_destination_dir(self):
        dummy_file = self.test_root / "AOK.ELF"
        dummy_file.write_bytes(b"content")
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "import_owned_game.py"),
            "--source",
            str(dummy_file),
            "--dest-dir",
            str(self.test_root / "game" / "WRONG-SERIAL"),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertTrue(
            "checksum mismatch" in res.stderr.lower() or "unexpected destination" in res.stderr.lower()
        )

    def test_import_valid_and_idempotent(self):
        # We test with the actual local source file if present
        local_src = Path("/home/samuelcaldas/source/repos/aoe/orig/SLES-50282/AOK.ELF")
        if not local_src.exists():
            self.skipTest("Local original AOK.ELF not found for end-to-end import test")

        dest_dir = self.test_root / "game" / "SLES-50282"
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "import_owned_game.py"),
            "--source",
            str(local_src),
            "--dest-dir",
            str(dest_dir),
        ]
        
        # 1. First import: success
        res1 = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res1.returncode, 0, f"Import failed: {res1.stderr}")
        self.assertIn("successfully imported", res1.stdout.lower())
        self.assertTrue((dest_dir / "AOK.ELF").exists())

        # 2. Re-import (idempotent): success
        res2 = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res2.returncode, 0, f"Idempotent re-import failed: {res2.stderr}")
        self.assertIn("already imported and valid", res2.stdout.lower())

        # 3. Conflicting destination: error
        (dest_dir / "AOK.ELF").write_bytes(b"corrupted content")
        res3 = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res3.returncode, 0)
        self.assertIn("conflicting file exists", res3.stderr.lower())

    def test_verify_missing_game(self):
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_owned_game.py"),
            "--game-dir",
            str(self.test_root / "game"),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("missing", res.stderr.lower() + res.stdout.lower())

    def test_verify_corrupted_game(self):
        dest_dir = self.test_root / "game" / "SLES-50282"
        dest_dir.mkdir(parents=True, exist_ok=True)
        target_file = dest_dir / "AOK.ELF"
        target_file.write_bytes(b"corrupted content")
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_owned_game.py"),
            "--game-dir",
            str(self.test_root / "game"),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("checksum mismatch", res.stderr.lower() + res.stdout.lower())

    def test_verify_valid_game(self):
        local_src = Path("/home/samuelcaldas/source/repos/aoe/orig/SLES-50282/AOK.ELF")
        if not local_src.exists():
            self.skipTest("Local original AOK.ELF not found for verify test")

        dest_dir = self.test_root / "game" / "SLES-50282"
        dest_dir.mkdir(parents=True, exist_ok=True)
        shutil.copy2(local_src, dest_dir / "AOK.ELF")

        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_owned_game.py"),
            "--game-dir",
            str(self.test_root / "game"),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"Verification failed: {res.stderr}")
        self.assertIn("verification success", res.stdout.lower())
