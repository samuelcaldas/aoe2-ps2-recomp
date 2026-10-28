"""Window <-> logical-screen mapping of the desktop presentation, mirroring
aoe2::input::PresentationTransform (input/src/presentation_transform.cpp).

The runtime aspect-fits the PAL GS frame (640x512) into the window; the game's
640x448 logical screen fills that frame. Scenario points are logical pixels.
"""
from __future__ import annotations

FRAME = (640, 512)
LOGICAL = (640, 448)


def frame_destination(window: tuple) -> tuple:
    """Return (x, y, width, height) of the frame in window pixels; raises ValueError for an empty window."""
    width, height = window
    if width <= 0 or height <= 0:
        raise ValueError(f"window extent must be positive: {window}")
    scale = min(width / FRAME[0], height / FRAME[1])
    frame_width, frame_height = FRAME[0] * scale, FRAME[1] * scale
    return ((width - frame_width) / 2.0, (height - frame_height) / 2.0, frame_width, frame_height)


def to_window(point: tuple, window: tuple) -> tuple:
    """Map a logical pixel to window pixels (not clamped)."""
    x, y, width, height = frame_destination(window)
    return (x + point[0] * width / LOGICAL[0], y + point[1] * height / LOGICAL[1])


def to_logical(point: tuple, window: tuple) -> tuple:
    """Map a window pixel to a logical pixel, clamped to the logical screen."""
    x, y, width, height = frame_destination(window)
    logical_x = (point[0] - x) * LOGICAL[0] / width
    logical_y = (point[1] - y) * LOGICAL[1] / height
    return (min(max(logical_x, 0.0), LOGICAL[0] - 1.0), min(max(logical_y, 0.0), LOGICAL[1] - 1.0))
