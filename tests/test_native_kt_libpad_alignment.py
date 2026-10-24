"""Observe the packed Sony alignment command without a production device test hook."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def alignment_command(output, state_source=None):
    sources = [ROOT / 'pad/src' / name for name in
               ('kt_libpad_model.cpp', 'kt_libpad_math.cpp', 'pad_device.cpp', 'pad_report.cpp')]
    sources.append(state_source or ROOT / 'pad/src/kt_libpad_state.cpp')
    return ['g++', '-std=c++20', f'-I{ROOT / "pad/include"}', f'-I{ROOT / "runtime/include"}',
            str(ROOT / 'tests/cpp/kt_libpad_alignment_buffer_tests.cpp'),
            *map(str, sources), '-Wl,--wrap=_ZN4aoe23pad9PadDevice11SetActAlignEiiPKh',
            '-o', str(output)]


class TestNativeKtLibpadAlignment(unittest.TestCase):
    def test_alignment_command_packs_by_byte_offset(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'alignment-test'
            result = subprocess.run(alignment_command(output), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(subprocess.run([str(output)]).returncode, 0)
