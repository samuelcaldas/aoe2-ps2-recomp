#!/usr/bin/env python3
"""Check that the guest cursor settled on each logical calibration point.

Reads a game log produced with AOE2_LOG_POINTER=1 (`[host-pointer]` lines) and,
for each expected logical point, compares the last guest cursor reported while
the host pointer mapped to that point. The game clamps its cursor to 0..628 x
0..431 (docs/hd-render-evidence.md), so targets are clamped the same way.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

CURSOR_MAX = (628, 431)
TOLERANCE = 1
LINE = re.compile(r"\[host-pointer\] window=\S+ target=([-\d.]+),([-\d.]+) cursor=(-?\d+),(-?\d+)")


class CalibrationError(RuntimeError):
    pass


def settled_errors(log: str, points: list) -> list:
    """Return (|dx|, |dy|) per point; raises CalibrationError if a point never appears."""
    samples = [tuple(float(value) for value in match.groups()) for match in LINE.finditer(log)]
    errors = []
    for point in points:
        matching = [sample for sample in samples if abs(sample[0] - point[0]) < 0.5 and abs(sample[1] - point[1]) < 0.5]
        if not matching:
            raise CalibrationError(f"no pointer sample targets {point}")
        _, _, cursor_x, cursor_y = matching[-1]
        expected = (min(point[0], CURSOR_MAX[0]), min(point[1], CURSOR_MAX[1]))
        errors.append((round(abs(cursor_x - expected[0])), round(abs(cursor_y - expected[1]))))
    return errors


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--point", action="append", required=True, help="logical x,y")
    args = parser.parse_args(argv)
    points = [tuple(int(value) for value in text.split(",")) for text in args.point]
    errors = settled_errors(args.log.read_text(errors="replace"), points)
    for point, error in zip(points, errors):
        print(f"point={point} error={error}")
    return 0 if all(max(error) <= TOLERANCE for error in errors) else 1


if __name__ == "__main__":
    sys.exit(main())
