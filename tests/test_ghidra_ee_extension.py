#!/usr/bin/env python3
"""Tests for the pinned Emotion Engine Ghidra extension installer."""
import hashlib
import io
import tempfile
import unittest
import zipfile
from pathlib import Path

from tools import install_ghidra_ee_extension as installer


def make_extension_zip(path: Path, marker: str = "r5900") -> str:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        archive.writestr(f"{installer.EXTENSION_DIR_NAME}/extension.properties", "version=12.1.4\n")
        archive.writestr(f"{installer.EXTENSION_DIR_NAME}/data/languages/emotionengine.ldefs", marker)
    path.write_bytes(buffer.getvalue())
    return hashlib.sha256(buffer.getvalue()).hexdigest()


class TestEeExtensionInstaller(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.archive = self.root / "extension.zip"
        self.extensions_dir = self.root / "Extensions"

    def tearDown(self):
        self.temporary_directory.cleanup()

    def test_installs_archive_with_matching_digest(self):
        digest = make_extension_zip(self.archive)
        status = installer.install_archive(self.archive, digest, self.extensions_dir)
        self.assertEqual(status, installer.INSTALLED)
        self.assertTrue(installer.is_installed(self.extensions_dir))

    def test_rejects_archive_with_wrong_digest(self):
        make_extension_zip(self.archive)
        with self.assertRaises(installer.ExtensionInstallError):
            installer.install_archive(self.archive, "0" * 64, self.extensions_dir)
        self.assertFalse(installer.is_installed(self.extensions_dir))

    def test_second_install_is_a_no_op(self):
        digest = make_extension_zip(self.archive)
        installer.install_archive(self.archive, digest, self.extensions_dir)
        self.assertEqual(installer.install_archive(self.archive, digest, self.extensions_dir), installer.ALREADY_INSTALLED)

    def test_unknown_ghidra_version_is_rejected(self):
        with self.assertRaises(installer.ExtensionInstallError):
            installer.release_for("ghidra_9.0_PUBLIC")

    def test_pinned_release_exists_for_project_ghidra(self):
        release = installer.release_for("ghidra_12.1.4_PUBLIC")
        self.assertEqual(len(release.sha256), 64)
        self.assertIn("12.1.4", release.asset)


if __name__ == "__main__":
    unittest.main()
