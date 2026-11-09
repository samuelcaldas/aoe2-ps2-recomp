"""Input strategies. Each backend turns logical scenario actions into one platform's input."""

from __future__ import annotations

import time
from typing import Callable, Sequence

from tools.presentation_geometry import to_window
from tools.scenarios.steps import Point

CommandRunner = Callable[[Sequence[str]], object]

# Timings copied from the original desktop driver; the guest needs them to register a click.
POINTER_APPROACH_SECONDS = 1.5
POINTER_PRESS_SECONDS = 0.8
KEY_HOLD_SECONDS = 0.3


class XdotoolMouseBackend:
    """PC: pointer clicks and key presses sent to the game's X window via xdotool."""

    def __init__(
        self,
        run: CommandRunner,
        window: str,
        window_size: tuple[int, int],
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        if not window:
            raise ValueError("an X window id is required")
        self._run = run
        self._window = window
        self._window_size = window_size
        self._sleep = sleep

    def window_point(self, point: Point) -> tuple[int, int]:
        """Logical (640x448) point to window pixels, as the presentation maps them."""
        x, y = to_window(point, self._window_size)
        return int(x), int(y)

    def _move(self, point: Point) -> None:
        x, y = self.window_point(point)
        self._run(["xdotool", "mousemove", "--window", self._window, str(x), str(y)])

    def tap(self, point: Point) -> None:
        self._move(point)
        self._sleep(POINTER_APPROACH_SECONDS)
        self._run(["xdotool", "mousedown", "1"])
        self._sleep(POINTER_PRESS_SECONDS)
        self._run(["xdotool", "mouseup", "1"])

    def hover(self, point: Point) -> None:
        self._move(point)

    def press(self, button: str) -> None:
        if not button:
            raise ValueError("a key name is required")
        self._run(["xdotool", "keydown", button])
        self._sleep(KEY_HOLD_SECONDS)
        self._run(["xdotool", "keyup", button])


# Logical button names used by scenario steps -> xdotool key names for the Ryujinx pad mapping
# (tools/validate_switch.py writes these bindings into the emulator config).
PAD_KEYS = {
    "up": "Up",
    "down": "Down",
    "left": "Left",
    "right": "Right",
    "cross": "z",
    "circle": "x",
    "start": "Return",
    "select": "Minus",
}


class UnsupportedInputError(ValueError):
    """Raised when a step asks for input the platform does not have."""


class RyujinxPadBackend:
    """Switch: the joystick drives the menus. Pointer taps exist only for the boot sequence,
    which the pad cannot reach yet; the Random Map flow itself uses presses only."""

    def __init__(self, send_key: Callable[[str], object], click: Callable[[Point], object]) -> None:
        self._send_key = send_key
        self._click = click

    def press(self, button: str) -> None:
        try:
            key = PAD_KEYS[button]
        except KeyError as error:
            raise UnsupportedInputError(f"no pad mapping for {button!r}") from error
        self._send_key(key)

    def tap(self, point: Point) -> None:
        self._click(point)

    def hover(self, point: Point) -> None:
        raise UnsupportedInputError("the pad has no pointer; hover steps are not valid on Switch")
