"""Measured logical (640x448) points for the Single Player → Random Map → Start Game menus.

A value of None means the point has not been measured from a captured screenshot yet.
Steps built from a None point raise UnresolvedLayoutError, so the scenario cannot run
on a guessed coordinate.
"""

from __future__ import annotations

from dataclasses import dataclass

from tools.scenarios.steps import Point


@dataclass(frozen=True)
class MenuLayout:
    single_player: Point | None
    random_map: Point | None
    game_settings_neutral: Point | None
    game_settings_ok: Point | None
    start_game: Point | None


# Measured from .playwright-mcp/pc/ captures (Xvfb 640x448, PC Release, 2026-11-08). The captures are
# window pixels; the logical values below are their inverse through presentation_geometry (a 640x448
# window letterboxes the 640x512 frame: x is scaled by 0.875 and offset by 40, y is unchanged).
# - single_player / random_map: Single Player submenu rows (row 2 is at y=124, logical).
# - game_settings_neutral: window (120, 290), empty parchment that closes an open Map Type list.
# - game_settings_ok: window (258, 408), centre of the OK button (box 212..305 x 396..420).
# - start_game: window (293, 409), centre of the Start Game button (box 245..342 x 400..420).
DESKTOP_MENUS = MenuLayout(
    single_player=(458, 85),
    random_map=(458, 124),
    game_settings_neutral=(91, 290),
    game_settings_ok=(249, 408),
    start_game=(289, 409),
)

# Logical points checked by tools/check_pointer_calibration.py.
CALIBRATION_POINTS: tuple[Point, ...] = ((100, 120), (520, 380))
