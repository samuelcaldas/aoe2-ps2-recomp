"""Test syntax and direct compilation of native mouse and keyboard module adapters."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / "third_party/PS2Recomp/ps2xRuntime"


def compile_command(source, output):
    includes = [
        RUNTIME.parent / "ps2xIOP/include",
        RUNTIME / "include",
        RUNTIME / "src",
        RUNTIME / "src/lib/Kernel",
        RUNTIME / "src/lib/Kernel/Syscalls",
        ROOT / "build/docker-desktop/_deps/raylib-src/src",
    ]
    includes += [
        ROOT / name / "include"
        for name in (
            "game_hooks",
            "mouse",
            "keyboard",
            "pad",
            "input",
            "runtime",
            "kernel",
            "net",
            "renderer",
            "core",
        )
    ]
    return [
        "g++",
        "-std=c++20",
        "-msse4.1",
        "-ffunction-sections",
        "-fdata-sections",
        "-c",
        *[f"-I{path}" for path in includes],
        str(source),
        "-o",
        str(output),
    ]


class TestNativeInputModuleCompilation(unittest.TestCase):
    @unittest.skipUnless((RUNTIME / "include/ps2_runtime.h").is_file(), "pinned runtime absent")
    def test_compile_mouse_module(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / "mouse_module.o"
            result = subprocess.run(
                compile_command(ROOT / "game_hooks/src/kcet_mouse_module.cpp", output),
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(output.exists())

    @unittest.skipUnless((RUNTIME / "include/ps2_runtime.h").is_file(), "pinned runtime absent")
    def test_compile_keyboard_module(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / "keyboard_module.o"
            result = subprocess.run(
                compile_command(ROOT / "game_hooks/src/kcet_keyboard_module.cpp", output),
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(output.exists())

    @unittest.skipUnless((RUNTIME / "include/ps2_runtime.h").is_file(), "pinned runtime absent")
    def test_compile_native_input_services(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / "input_services.o"
            result = subprocess.run(
                compile_command(ROOT / "game_hooks/src/native_input_services.cpp", output),
                capture_output=True,
                text=True,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(output.exists())


if __name__ == "__main__":
    unittest.main()
