"""Command objects for scenario steps.

Each step is an immutable value that performs one input or wait against a
context (see runner.ScenarioContext) and reports whether the expected effect
was observed. Steps hold no platform knowledge; backends translate them.
"""

from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Protocol

Point = tuple[int, int]
_KEBAB_NAME = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")


class UnresolvedLayoutError(ValueError):
    """Raised when a scenario refers to a point that has not been measured yet."""


class StepContext(Protocol):
    """What a step may use: the input backend and the guest-event watcher."""

    backend: "InputBackend"

    def await_marker(self, marker: bytes, timeout: float) -> bool: ...


class InputBackend(Protocol):
    """Platform strategy: how a logical action reaches the guest."""

    def tap(self, point: Point) -> None: ...

    def hover(self, point: Point) -> None: ...

    def press(self, button: str) -> None: ...


@dataclass(frozen=True, kw_only=True)
class Step:
    """Common fields: a unique name, the settle time after the action, and whether to capture a checkpoint."""

    name: str
    settle_seconds: float = 0.0
    checkpoint: bool = False

    def __post_init__(self) -> None:
        if not _KEBAB_NAME.match(self.name):
            raise ValueError(f"step name must be kebab-case: {self.name!r}")
        if self.settle_seconds < 0:
            raise ValueError(f"settle must not be negative: {self.name}")

    def perform(self, context: StepContext) -> bool:
        """Run the action; return False when the expected effect was not observed."""
        return True


def _require_point(name: str, point: Point | None) -> Point:
    if point is None:
        raise UnresolvedLayoutError(f"step {name!r} has no measured screen point")
    if len(point) != 2 or point[0] < 0 or point[1] < 0:
        raise ValueError(f"step {name!r} point must be two non-negative pixels: {point}")
    return point


@dataclass(frozen=True, kw_only=True)
class Tap(Step):
    """Click a logical point."""

    point: Point

    def __post_init__(self) -> None:
        super().__post_init__()
        _require_point(self.name, self.point)

    def perform(self, context: StepContext) -> bool:
        context.backend.tap(self.point)
        return True


@dataclass(frozen=True, kw_only=True)
class Hover(Step):
    """Move the pointer to a logical point without clicking."""

    point: Point

    def __post_init__(self) -> None:
        super().__post_init__()
        _require_point(self.name, self.point)

    def perform(self, context: StepContext) -> bool:
        context.backend.hover(self.point)
        return True


@dataclass(frozen=True, kw_only=True)
class Press(Step):
    """Press a named controller or key button."""

    button: str

    def __post_init__(self) -> None:
        super().__post_init__()
        if not self.button:
            raise ValueError(f"step {self.name!r} needs a button")

    def perform(self, context: StepContext) -> bool:
        context.backend.press(self.button)
        return True


@dataclass(frozen=True, kw_only=True)
class Wait(Step):
    """Let the guest run for the settle time without input."""


@dataclass(frozen=True, kw_only=True)
class AwaitMarker(Step):
    """Wait until the guest log shows a marker; fails on timeout or guest exit."""

    marker: bytes
    timeout_seconds: float

    def __post_init__(self) -> None:
        super().__post_init__()
        if not self.marker:
            raise ValueError(f"step {self.name!r} needs a marker")
        if self.timeout_seconds <= 0:
            raise ValueError(f"step {self.name!r} needs a positive timeout")

    def perform(self, context: StepContext) -> bool:
        return context.await_marker(self.marker, self.timeout_seconds)
