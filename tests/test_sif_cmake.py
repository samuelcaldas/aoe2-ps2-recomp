"""The SIF ownership bridge must fail closed when its upstream block changes."""
import hashlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "third_party/PS2Recomp/ps2xRuntime/src/lib/Kernel/Stubs/SIF.cpp"


@unittest.skipUnless(shutil.which("cmake") and SOURCE.is_file(), "local runtime and CMake required")
class SifBridgeTest(unittest.TestCase):
    def configure(self, text):
        with tempfile.TemporaryDirectory(prefix="aoe2-sif-cmake-") as folder:
            root = Path(folder)
            source = root / "SIF.cpp"
            source.write_text(text)
            before = hashlib.sha256(source.read_bytes()).digest()
            (root / "CMakeLists.txt").write_text(
                "cmake_minimum_required(VERSION 3.20)\nproject(sif_bridge LANGUAGES CXX)\n"
                'add_library(runtime STATIC "${CMAKE_CURRENT_SOURCE_DIR}/SIF.cpp")\n'
                f'include("{ROOT / "cmake/Aoe2Sif.cmake"}")\n'
                'aoe2_install_sif(runtime "${CMAKE_CURRENT_SOURCE_DIR}/SIF.cpp")\n'
            )
            result = subprocess.run(["cmake", "-S", str(root), "-B", str(root / "build")],
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(hashlib.sha256(source.read_bytes()).digest(), before)
            return result

    def test_verified_runtime_configures_without_editing_upstream(self):
        result = self.configure(SOURCE.read_text())
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_generic_runtime_without_game_injection_is_left_alone(self):
        result = self.configure("void sif_service() {}\n")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_changed_legacy_block_requires_review(self):
        original = SOURCE.read_text()
        changed = original.replace("const uint32_t total = pending + 1u;",
                                   "const uint32_t total = pending + 2u;")
        self.assertNotEqual(changed, original)
        result = self.configure(changed)
        self.assertNotEqual(result.returncode, 0, "changed runtime block was silently removed")
        self.assertIn("completion block changed", result.stdout + result.stderr)
