#!/usr/bin/env python3
"""Classify validation screenshots as real guest frames or the runtime fallback.

PS2Recomp presents a solid magenta frame until the guest first presents one
(ps2_runtime.cpp), so a screenshot dominated by that colour means the guest has
produced no output. Pixel statistics come from ImageMagick (`magick`), passed in
as an executor so the classification logic stays testable without images.
"""
from __future__ import annotations

import enum
import re
import subprocess
from dataclasses import dataclass
from typing import Callable, Sequence

FALLBACK_RGB = (255, 0, 255)
BLACK_RGB = (0, 0, 0)
NEAR_COLOR_TOLERANCE = 24
FALLBACK_SHARE = 0.98
MIN_DISTINCT_COLORS = 16
HISTOGRAM_LINE = re.compile(r"^\s*(\d+):\s*\((\d+),(\d+),(\d+)\)")
METRIC_LINE = re.compile(r"^\s*(\d+)")

Executor = Callable[[Sequence[str]], str]


class FrameAnalysisError(RuntimeError):
    """Raised when ImageMagick output cannot be read."""


class FrameKind(str, enum.Enum):
    BLANK = "blank"
    SOLID_FALLBACK = "solid_fallback"
    LOW_DETAIL = "low_detail"
    REAL_FRAME = "real_frame"


@dataclass(frozen=True)
class ColorCount:
    rgb: tuple
    count: int


@dataclass(frozen=True)
class FrameSeries:
    kinds: tuple
    real_frames: int
    changed: bool


def run_command(args: Sequence[str]) -> str:
    """Run a command and return stdout followed by stderr (ImageMagick reports some metrics on stderr)."""
    result = subprocess.run(list(args), capture_output=True, text=True, errors="replace")
    return result.stdout + result.stderr


def parse_histogram(text: str) -> list:
    colors = []
    for line in text.splitlines():
        match = HISTOGRAM_LINE.match(line)
        if match:
            colors.append(ColorCount((int(match[2]), int(match[3]), int(match[4])), int(match[1])))
    return colors


def parse_metric(text: str) -> int:
    match = METRIC_LINE.match(text)
    if match is None:
        raise FrameAnalysisError(f"no pixel count in ImageMagick output: {text.strip()!r}")
    return int(match[1])


def channel_distance(first: tuple, second: tuple) -> int:
    return sum(abs(a - b) for a, b in zip(first, second))


def classify_histogram(colors: list) -> FrameKind:
    content = [color for color in colors if channel_distance(color.rgb, BLACK_RGB) > NEAR_COLOR_TOLERANCE]
    content_pixels = sum(color.count for color in content)
    if content_pixels == 0:
        return FrameKind.BLANK
    fallback_pixels = sum(color.count for color in content
                          if channel_distance(color.rgb, FALLBACK_RGB) <= NEAR_COLOR_TOLERANCE)
    if fallback_pixels / content_pixels >= FALLBACK_SHARE:
        return FrameKind.SOLID_FALLBACK
    if len(content) < MIN_DISTINCT_COLORS:
        return FrameKind.LOW_DETAIL
    return FrameKind.REAL_FRAME


def classify_frame(path: str, executor: Executor = run_command) -> FrameKind:
    histogram = executor(["magick", str(path), "-format", "%c", "histogram:info:-"])
    return classify_histogram(parse_histogram(histogram))


def differing_pixels(first: str, second: str, executor: Executor = run_command) -> int:
    output = executor(["magick", "compare", "-metric", "AE", str(first), str(second), "null:"])
    return parse_metric(output)


def analyze_frames(paths: Sequence[str], executor: Executor = run_command) -> FrameSeries:
    """Classify each screenshot and report whether two real frames differ (first real vs last real)."""
    kinds = tuple(classify_frame(path, executor) for path in paths)
    real = [path for path, kind in zip(paths, kinds) if kind is FrameKind.REAL_FRAME]
    changed = len(real) >= 2 and differing_pixels(real[0], real[-1], executor) > 0
    return FrameSeries(kinds=kinds, real_frames=len(real), changed=changed)
