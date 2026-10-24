"""Regress the original conditional-move polarity for every query outcome."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class TestNativeKtLibpadPressure(unittest.TestCase):
    def test_pressure_query_flag_polarity(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            output = Path(directory) / 'pressure-test'
            sources = [ROOT / 'pad/src' / name for name in
                       ('kt_libpad_model.cpp', 'kt_libpad_math.cpp', 'kt_libpad_state.cpp',
                        'pad_device.cpp', 'pad_report.cpp')]
            command = ['g++', '-std=c++20', f'-I{ROOT / "pad/include"}', f'-I{ROOT / "runtime/include"}',
                       str(ROOT / 'tests/cpp/kt_libpad_pressure_flag_tests.cpp'),
                       *map(str, sources),
                       '-Wl,--wrap=_ZNK4aoe23pad9PadDevice13InfoPressModeEii',
                       '-o', str(output)]
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(subprocess.run([str(output)]).returncode, 0)
