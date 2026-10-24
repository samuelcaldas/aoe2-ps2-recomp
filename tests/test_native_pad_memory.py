"""Compile focused runtime-boundary tests without building the game."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = ROOT / 'third_party/PS2Recomp/ps2xRuntime'


def compile_command(source, output):
    includes = [RUNTIME.parent / 'ps2xIOP/include', RUNTIME / 'include', RUNTIME / 'src', RUNTIME / 'src/lib/Kernel',
                RUNTIME / 'src/lib/Kernel/Syscalls']
    includes += [ROOT / name / 'include' for name in
                 ('game_hooks', 'pad', 'input', 'runtime', 'kernel', 'net', 'renderer', 'core')]
    return ['g++', '-std=c++20', '-msse4.1', '-ffunction-sections', '-fdata-sections',
            *[f'-I{path}' for path in includes], str(source),
            '-Wl,--gc-sections', '-o', str(output)]


class TestNativePadMemory(unittest.TestCase):
    @unittest.skipUnless((RUNTIME / 'include/ps2_runtime.h').is_file(), 'pinned runtime absent')
    def test_whole_access_bounds(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'pad-memory-test'
            result = subprocess.run(compile_command(ROOT / 'tests/cpp/native_pad_memory_tests.cpp', output),
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    @unittest.skipUnless((RUNTIME / 'include/ps2_runtime.h').is_file(), 'pinned runtime absent')
    def test_snapshot_whole_slot_bounds(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'snapshot-test'
            result = subprocess.run(compile_command(ROOT / 'tests/cpp/native_pad_snapshot_tests.cpp', output),
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr.count('[pad-snapshot]'), 19, result.stderr)
            snapshots = [line for line in result.stderr.splitlines() if line.startswith('[pad-snapshot]')]
            self.assertTrue(all(len(line.split(' bytes=')[1]) == 1024 for line in snapshots))
            self.assertIn('n=64 slot=0', result.stderr)
            self.assertNotIn('n=17 slot=0', result.stderr)
            self.assertIn('observer=StepSlot enabled=0', result.stderr)
            self.assertIn('reason=invalid-slot-range', result.stderr)
            self.assertIn('reason=slot-outside-work-array', result.stderr)
            self.assertIn('n=1 slot=0 samples0=17 samples1=9', result.stderr)
            self.assertIn('n=1 slot=1 samples0=17 samples1=9', result.stderr)
