"""Configure synthetic runtime targets to check strict DECI2 source selection."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]


@unittest.skipUnless(shutil.which("cmake"), "cmake is required")
class Deci2SourceSelectionTests(unittest.TestCase):
    def configure(self, copies):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "Deci2.cpp"
            source.write_text("// synthetic SDK translation unit\n")
            (root / "other.cpp").write_text("// synthetic unrelated translation unit\n")
            sources = " ".join(f'"{source}"' for _ in range(copies))
            (root / "CMakeLists.txt").write_text(
                'cmake_minimum_required(VERSION 3.20)\n'
                'project(deci2_selection LANGUAGES CXX)\n'
                'add_library(aoe2_deci2_host INTERFACE)\n'
                f'add_library(fixture STATIC other.cpp {sources})\n'
                f'include("{REPO_ROOT}/cmake/Aoe2Deci2.cmake")\n'
                f'aoe2_install_deci2(fixture "{source}" "{REPO_ROOT}")\n'
                'get_target_property(selected fixture SOURCES)\n'
                f'if("{source}" IN_LIST selected)\n'
                'message(FATAL_ERROR "Original DECI2 source was retained")\n'
                'endif()\n'
            )
            return subprocess.run(
                ["cmake", "-S", str(root), "-B", str(root / "build")],
                capture_output=True, text=True, check=False,
            )

    def test_replaces_exactly_one_source(self):
        result = self.configure(1)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_rejects_missing_source(self):
        result = self.configure(0)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected exactly one upstream DECI2 syscall source", result.stderr)

    def test_rejects_duplicate_sources(self):
        result = self.configure(2)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("Expected exactly one upstream DECI2 syscall source", result.stderr)
