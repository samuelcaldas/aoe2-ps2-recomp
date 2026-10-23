import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.runtime_patches import PatchError, PatchState, apply_patches, patch_state

ORIGINAL = "one\ntwo\nthree\n"
PATCH = """--- a/file.txt
+++ b/file.txt
@@ -1,3 +1,3 @@
 one
-two
+TWO
 three
"""


def git(*args, cwd):
    subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True)


class TestRuntimePatches(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        root = Path(self.temp.name)
        self.repo = root / "repo"
        self.patches = root / "patches"
        self.repo.mkdir()
        self.patches.mkdir()
        git("init", "-q", cwd=self.repo)
        (self.repo / "file.txt").write_text(ORIGINAL)
        (self.patches / "0001-change.patch").write_text(PATCH)

    def tearDown(self):
        self.temp.cleanup()

    def test_applies_missing_patch_and_is_idempotent(self):
        self.assertEqual(patch_state(self.repo, self.patches / "0001-change.patch"), PatchState.MISSING)
        self.assertEqual(apply_patches(self.repo, self.patches), ["0001-change.patch"])
        self.assertEqual((self.repo / "file.txt").read_text(), "one\nTWO\nthree\n")
        self.assertEqual(patch_state(self.repo, self.patches / "0001-change.patch"), PatchState.APPLIED)
        self.assertEqual(apply_patches(self.repo, self.patches), [])

    def test_relative_patch_paths_are_resolved_before_changing_directory(self):
        import os
        previous = os.getcwd()
        os.chdir(self.patches)
        try:
            self.assertEqual(patch_state(self.repo, Path("0001-change.patch")), PatchState.MISSING)
        finally:
            os.chdir(previous)

    def test_conflicting_tree_fails_without_changing_files(self):
        (self.repo / "file.txt").write_text("one\nzzz\nthree\n")
        with self.assertRaises(PatchError):
            apply_patches(self.repo, self.patches)
        self.assertEqual((self.repo / "file.txt").read_text(), "one\nzzz\nthree\n")

    def test_applies_in_name_order_and_stops_on_first_failure(self):
        (self.patches / "0002-bad.patch").write_text(PATCH.replace("-two", "-nope").replace("+TWO", "+OTHER"))
        with self.assertRaises(PatchError):
            apply_patches(self.repo, self.patches)
        self.assertEqual((self.repo / "file.txt").read_text(), "one\nTWO\nthree\n")

    def test_missing_patch_directory_is_an_error(self):
        with self.assertRaises(PatchError):
            apply_patches(self.repo, self.patches / "absent")


if __name__ == "__main__":
    unittest.main()
