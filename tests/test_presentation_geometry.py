"""The scenario driver maps logical points with the same vectors as tests/cpp/presentation_transform_tests.cpp."""
import unittest

from tools.presentation_geometry import FRAME, LOGICAL, frame_destination, to_logical, to_window


class TestPresentationGeometry(unittest.TestCase):
    def assertPoint(self, actual, expected):
        self.assertAlmostEqual(actual[0], expected[0], places=2)
        self.assertAlmostEqual(actual[1], expected[1], places=2)

    def test_pal_frame_holds_stretched_logical_screen(self):
        self.assertEqual(FRAME, (640, 512))
        self.assertEqual(LOGICAL, (640, 448))

    def test_frame_destination_at_1080p(self):
        destination = frame_destination((1920, 1080))
        for actual, expected in zip(destination, (285.0, 0.0, 1350.0, 1080.0)):
            self.assertAlmostEqual(actual, expected, places=2)

    def test_to_window_at_720p(self):
        self.assertPoint(to_window((0, 0), (1280, 720)), (190.0, 0.0))
        self.assertPoint(to_window((640, 448), (1280, 720)), (1090.0, 720.0))

    def test_native_window_keeps_logical_center(self):
        self.assertPoint(to_window((320, 224), (640, 448)), (320.0, 224.0))
        self.assertPoint(to_logical((40, 0), (640, 448)), (0.0, 0.0))

    def test_to_logical_clamps_pillarbox_at_1080p(self):
        self.assertPoint(to_logical((960, 540), (1920, 1080)), (320.0, 224.0))
        self.assertPoint(to_logical((10, 1079), (1920, 1080)), (0.0, 447.0))

    def test_rejects_empty_window(self):
        with self.assertRaises(ValueError):
            to_window((0, 0), (0, 1080))


if __name__ == "__main__":
    unittest.main()
