#!/usr/bin/env python3
"""Tests for dist staging in tools/setup_game.py: the disc image is never copied."""
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


if __name__ == "__main__":
    unittest.main()
