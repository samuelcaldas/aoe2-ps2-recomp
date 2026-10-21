#!/usr/bin/env python3
"""Tests for the screenshot classifier that separates real frames from the fallback."""
import unittest

from tools import frame_analysis as frames


def histogram_line(count: int, rgb: tuple) -> str:
    red, green, blue = rgb
    return f"{count:>10}: ({red},{green},{blue}) #{red:02X}{green:02X}{blue:02X} srgb({red},{green},{blue})"


MAGENTA_HISTOGRAM = "\n".join([
    histogram_line(634880, (0, 0, 0)),
    histogram_line(286720, (255, 0, 255)),
])


def real_frame_histogram(colors: int = 40) -> str:
    lines = [histogram_line(10000, (0, 0, 0))]
    lines += [histogram_line(1000, (index * 5, 100, 200 - index)) for index in range(colors)]
    return "\n".join(lines)


class FakeExecutor:
    """Stands in for ImageMagick: histograms and pixel-difference counts keyed by file name."""

    def __init__(self, histograms: dict, differences: dict | None = None):
        self.histograms = histograms
        self.differences = differences or {}
        self.calls = []

    def __call__(self, args):
        self.calls.append(list(args))
        if args[1] == "compare":
            return self.differences[(args[4], args[5])]
        return self.histograms[args[1]]


class TestParseHistogram(unittest.TestCase):
    def test_reads_counts_and_colours_and_ignores_other_lines(self):
        text = "warning: something\n" + MAGENTA_HISTOGRAM + "\n"
        colors = frames.parse_histogram(text)
        self.assertEqual(colors, [
            frames.ColorCount((0, 0, 0), 634880),
            frames.ColorCount((255, 0, 255), 286720),
        ])


class TestClassifyHistogram(unittest.TestCase):
    def test_magenta_with_black_letterbox_is_solid_fallback(self):
        colors = frames.parse_histogram(MAGENTA_HISTOGRAM)
        self.assertIs(frames.classify_histogram(colors), frames.FrameKind.SOLID_FALLBACK)

    def test_all_black_is_blank(self):
        colors = frames.parse_histogram(histogram_line(921600, (0, 0, 0)))
        self.assertIs(frames.classify_histogram(colors), frames.FrameKind.BLANK)

    def test_many_colours_without_magenta_is_real_frame(self):
        colors = frames.parse_histogram(real_frame_histogram(colors=40))
        self.assertIs(frames.classify_histogram(colors), frames.FrameKind.REAL_FRAME)

    def test_few_colours_without_magenta_is_low_detail(self):
        colors = frames.parse_histogram(real_frame_histogram(colors=3))
        self.assertIs(frames.classify_histogram(colors), frames.FrameKind.LOW_DETAIL)

    def test_partial_magenta_is_not_the_fallback(self):
        text = "\n".join([histogram_line(10000, (0, 0, 0)), histogram_line(9000, (255, 0, 255)),
                           histogram_line(1000, (30, 30, 30))])
        colors = frames.parse_histogram(text)
        self.assertIsNot(frames.classify_histogram(colors), frames.FrameKind.SOLID_FALLBACK)


class TestAnalyzeFrames(unittest.TestCase):
    def test_two_different_real_frames_pass(self):
        executor = FakeExecutor(
            histograms={"a.png": real_frame_histogram(40), "b.png": real_frame_histogram(41)},
            differences={("a.png", "b.png"): "1234 (0.0188)"},
        )
        series = frames.analyze_frames(["a.png", "b.png"], executor)
        self.assertEqual(series.real_frames, 2)
        self.assertTrue(series.changed)

    def test_identical_real_frames_are_not_distinct(self):
        executor = FakeExecutor(
            histograms={"a.png": real_frame_histogram(40), "b.png": real_frame_histogram(40)},
            differences={("a.png", "b.png"): "0"},
        )
        series = frames.analyze_frames(["a.png", "b.png"], executor)
        self.assertEqual(series.real_frames, 2)
        self.assertFalse(series.changed)

    def test_fallback_only_series_has_no_real_frames(self):
        executor = FakeExecutor(histograms={"a.png": MAGENTA_HISTOGRAM, "b.png": MAGENTA_HISTOGRAM})
        series = frames.analyze_frames(["a.png", "b.png"], executor)
        self.assertEqual(series.real_frames, 0)
        self.assertFalse(series.changed)
        self.assertEqual(series.kinds, (frames.FrameKind.SOLID_FALLBACK, frames.FrameKind.SOLID_FALLBACK))

    def test_single_real_frame_cannot_show_progress(self):
        executor = FakeExecutor(histograms={"a.png": real_frame_histogram(40), "b.png": MAGENTA_HISTOGRAM})
        series = frames.analyze_frames(["a.png", "b.png"], executor)
        self.assertEqual(series.real_frames, 1)
        self.assertFalse(series.changed)

    def test_compare_uses_absolute_error_metric(self):
        executor = FakeExecutor(
            histograms={"a.png": real_frame_histogram(40), "b.png": real_frame_histogram(40)},
            differences={("a.png", "b.png"): "0"},
        )
        frames.analyze_frames(["a.png", "b.png"], executor)
        compare_call = next(call for call in executor.calls if call[1] == "compare")
        self.assertIn("AE", compare_call)


class TestDifferingPixels(unittest.TestCase):
    def test_reads_leading_integer_from_metric_output(self):
        self.assertEqual(frames.parse_metric("1234 (0.0188)"), 1234)
        self.assertEqual(frames.parse_metric("0"), 0)

    def test_rejects_output_without_metric(self):
        with self.assertRaises(frames.FrameAnalysisError):
            frames.parse_metric("magick: unable to open image")


if __name__ == "__main__":
    unittest.main()
