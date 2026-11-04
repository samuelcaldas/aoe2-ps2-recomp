#!/usr/bin/env python3
"""Behavioral tests for tools/extract_disc.py using synthetic ISO images."""
import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import extract_disc

FAKE_ELF = b"\x7fELF fake game executable"
FAKE_ELF_SHA1 = hashlib.sha1(FAKE_ELF).hexdigest()


@unittest.skipUnless(shutil.which("xorriso"), "xorriso is required")
class TestDiscExtract(unittest.TestCase):
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
        (tree / "EN" / "SMENU_CD.BIN").write_bytes(bytes(range(256)) * 20)
        (tree / "SYSTEM.CNF").write_text("BOOT2 = cdrom0:\\SLES_502.82;1\n")
        iso = self.root / "disc.iso"
        subprocess.run(["xorriso", "-as", "mkisofs", "-quiet", "-o", str(iso), str(tree)], check=True)
        return iso

    def test_extracts_tree_and_target_elf(self):
        extract_disc.extract_disc(self.make_iso(FAKE_ELF), self.destination, FAKE_ELF_SHA1)
        self.assertEqual((self.destination / "AOK.ELF").read_bytes(), FAKE_ELF)
        self.assertEqual((self.destination / "EN" / "SMENU_CD.BIN").read_bytes(), bytes(range(256)) * 20)

    def test_manifest_lbns_match_iso_bytes(self):
        iso = self.make_iso(FAKE_ELF)
        extract_disc.extract_disc(iso, self.destination, FAKE_ELF_SHA1)
        layout = json.loads((self.destination / "disc_layout.json").read_text())
        image = iso.read_bytes()
        self.assertEqual(len(layout["files"]), 3)
        for entry in layout["files"]:
            on_disc = image[entry["lbn"] * 2048: entry["lbn"] * 2048 + entry["size"]]
            self.assertEqual((self.destination / entry["path"]).read_bytes(), on_disc)

    def test_second_run_is_skipped(self):
        iso = self.make_iso(FAKE_ELF)
        extract_disc.extract_disc(iso, self.destination, FAKE_ELF_SHA1)
        status = extract_disc.extract_disc(iso, self.destination, FAKE_ELF_SHA1)
        self.assertEqual(status, extract_disc.ALREADY_EXTRACTED)

    def test_rejects_disc_whose_elf_does_not_match_target(self):
        iso = self.make_iso(b"\x7fELF another game")
        with self.assertRaises(extract_disc.DiscExtractError):
            extract_disc.extract_disc(iso, self.destination, FAKE_ELF_SHA1)
        self.assertFalse((self.destination / "EN").exists())


if __name__ == "__main__":
    unittest.main()
