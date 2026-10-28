"""Pointer calibration: the guest cursor settles on the logical point the host pointer maps to."""
import unittest

from tools.check_pointer_calibration import CalibrationError, settled_errors

LOG = """\
[host-pointer] window=1000,500 target=338.96,207.4 cursor=320,224
[host-pointer] window=496,253 target=100,120 cursor=300,200
[host-pointer] window=496,253 target=100,120 cursor=100,120
[host-pointer] window=1381,801 target=520,380 cursor=101,121
[host-pointer] window=1381,801 target=520,380 cursor=519,380
"""


class TestPointerCalibration(unittest.TestCase):
    def test_reports_last_cursor_error_per_target(self):
        errors = settled_errors(LOG, [(100, 120), (520, 380)])
        self.assertEqual(errors, [(0, 0), (1, 0)])

    def test_target_beyond_cursor_clamp_compares_with_clamp(self):
        log = "[host-pointer] window=1600,1070 target=639,447 cursor=628,431\n"
        self.assertEqual(settled_errors(log, [(639, 447)]), [(0, 0)])

    def test_missing_target_is_an_error(self):
        with self.assertRaises(CalibrationError):
            settled_errors(LOG, [(10, 10)])


if __name__ == "__main__":
    unittest.main()
