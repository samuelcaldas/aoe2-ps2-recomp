#!/usr/bin/env python3
"""Tests for dist staging in tools/setup_game.py."""
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import setup_game


class TestSetupGame(unittest.TestCase):
    def test_copy_game_folder_omits_iso_images(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            cd_root = root / "cd"
            (cd_root / "EN").mkdir(parents=True)
            (cd_root / "EN" / "A.BIN").write_bytes(b"a")
            (cd_root / "AOK.ELF").write_bytes(b"e")
            (cd_root / "aoe2.iso").write_bytes(b"i")
            with mock.patch.object(setup_game, "CD_ROOT", cd_root):
                setup_game.copy_game_folder(root / "out")
            self.assertTrue((root / "out" / "EN" / "A.BIN").is_file())
            self.assertTrue((root / "out" / "AOK.ELF").is_file())
            self.assertFalse((root / "out" / "aoe2.iso").exists())

    def test_stage_includes_iso_by_default_and_experimental_omits_it(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            cd_root = root / "cd"
            cd_root.mkdir()
            (cd_root / "AOK.ELF").write_bytes(b"elf")
            iso = root / "owned.iso"
            iso.write_bytes(b"disc")
            output = root / "build"
            output.mkdir()
            (output / "aoe2_game").write_bytes(b"binary")
            with mock.patch.object(setup_game, "CD_ROOT", cd_root), \
                 mock.patch.object(setup_game, "DIST", root / "dist"), \
                 mock.patch.object(setup_game, "build_output", return_value=output):
                target = setup_game.stage_platform("pc", "Release", iso)
                self.assertEqual((target / "aoe2.iso").read_bytes(), b"disc")
                with self.assertRaises(FileExistsError):
                    setup_game.stage_platform("pc", "Release")
                (target / "aoe2.iso").unlink()
                setup_game.stage_platform("pc", "Release")
                self.assertFalse((target / "aoe2.iso").exists())

    def test_default_requires_iso(self):
        with mock.patch.object(setup_game.extract_disc, "find_iso", return_value=None):
            self.assertEqual(setup_game.main(["--platform", "pc", "--skip-extract"]), 1)


if __name__ == "__main__":
    unittest.main()
