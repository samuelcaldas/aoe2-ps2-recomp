#!/usr/bin/env python3
"""Behavioral tests for tools/import_owned_disc.py using synthetic ISO images."""
import hashlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools import import_owned_disc as disc_import

REPO_ROOT = Path(__file__).resolve().parent.parent
SCRIPT = REPO_ROOT / "tools" / "import_owned_disc.py"
FAKE_ELF = b"\x7fELF fake game executable"
FAKE_ELF_SHA1 = hashlib.sha1(FAKE_ELF).hexdigest()


@unittest.skipUnless(shutil.which("xorriso") and shutil.which("7z"), "xorriso and 7z are required")
class TestDiscImport(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.destination = self.root / "game" / "SLES-50282"

    def tearDown(self):
        self.temporary_directory.cleanup()

    def make_iso(self, elf_bytes: bytes) -> Path:
        tree = self.root / "tree"
        (tree / "EN").mkdir(parents=True, exist_ok=True)
        (tree / "EN" / "AGE2_PS2.ELF").write_bytes(elf_bytes)
        (tree / "EN" / "SMENU_CD.BIN").write_bytes(b"overlay")
        (tree / "SYSTEM.CNF").write_text("BOOT2 = cdrom0:\\SLES_502.82;1\n")
        iso = self.root / "disc.iso"
        subprocess.run(["xorriso", "-as", "mkisofs", "-quiet", "-o", str(iso), str(tree)], check=True)
        return iso

    def test_extracts_disc_and_places_target_elf(self):
        iso = self.make_iso(FAKE_ELF)
        disc_import.import_disc(iso, self.destination, FAKE_ELF_SHA1)
        self.assertEqual((self.destination / "EN" / "SMENU_CD.BIN").read_bytes(), b"overlay")
        self.assertEqual((self.destination / "AOK.ELF").read_bytes(), FAKE_ELF)

    def test_rejects_disc_whose_elf_does_not_match_target(self):
        iso = self.make_iso(b"\x7fELF another game")
        with self.assertRaises(disc_import.DiscImportError):
            disc_import.import_disc(iso, self.destination, FAKE_ELF_SHA1)
        self.assertFalse((self.destination / "EN").exists())

    def test_reimport_of_same_disc_is_idempotent(self):
        iso = self.make_iso(FAKE_ELF)
        disc_import.import_disc(iso, self.destination, FAKE_ELF_SHA1)
        marker_before = (self.destination / disc_import.MARKER_NAME).read_text()
        self.assertEqual(disc_import.import_disc(iso, self.destination, FAKE_ELF_SHA1), disc_import.ALREADY_IMPORTED)
        self.assertEqual((self.destination / disc_import.MARKER_NAME).read_text(), marker_before)

    def test_refuses_conflicting_existing_elf(self):
        iso = self.make_iso(FAKE_ELF)
        self.destination.mkdir(parents=True)
        (self.destination / "AOK.ELF").write_bytes(b"different")
        with self.assertRaises(disc_import.DiscImportError):
            disc_import.import_disc(iso, self.destination, FAKE_ELF_SHA1)

    def test_cli_rejects_missing_iso(self):
        result = subprocess.run([sys.executable, str(SCRIPT), "--iso", str(self.root / "missing.iso")],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 1)
        self.assertIn("not found", result.stderr)


if __name__ == "__main__":
    unittest.main()
