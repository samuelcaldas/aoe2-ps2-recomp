"""Template Method runner: boot, perform steps in order, capture checkpoints, stop on the first failure."""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Callable, Sequence

from tools.scenarios.steps import InputBackend, Step

MarkerWaiter = Callable[[bytes, float], bool]
ScreenWaiter = Callable[[str, float], bool]


@dataclass(frozen=True)
class RunResult:
    passed: bool
    completed: tuple[str, ...]
    failed_step: str | None = None


def _no_screen_detector(state: str, timeout: float) -> bool:
    raise RuntimeError(f"screen state {state!r} requested but no screen detector is configured")


class CheckpointObserver:
    """Receives each checkpoint step; the default observer does nothing."""

    def on_checkpoint(self, step_name: str) -> None:
        return None


class ScenarioRunner:
    """Runs a step sequence against one platform backend.

    Subclasses override ``boot`` for platform start-up; the step sequence itself is shared.
    """

    def __init__(
        self,
        *,
        backend: InputBackend,
        await_marker: MarkerWaiter,
        is_alive: Callable[[], bool],
        await_screen: ScreenWaiter | None = None,
        observer: CheckpointObserver | None = None,
        settle_scale: float = 1.0,
        sleep: Callable[[float], None] = time.sleep,
    ) -> None:
        if settle_scale <= 0:
            raise ValueError("settle_scale must be positive")
        self.backend = backend
        self._await_marker = await_marker
        self._is_alive = is_alive
        self._await_screen = await_screen or _no_screen_detector
        self._observer = observer or CheckpointObserver()
        self._settle_scale = settle_scale
        self._sleep = sleep

    def boot(self) -> None:
        """Platform start-up before the first step; the default has nothing to do."""

    def await_marker(self, marker: bytes, timeout: float) -> bool:
        return self._await_marker(marker, timeout * self._settle_scale)

    def await_screen(self, state: str, timeout: float) -> bool:
        return self._await_screen(state, timeout * self._settle_scale)

    def run(self, steps: Sequence[Step]) -> RunResult:
        self.boot()
        completed: list[str] = []
        for step in steps:
            effect_seen = step.perform(self)
            settle = step.settle_seconds * self._settle_scale
            if settle > 0:
                self._sleep(settle)
            if step.checkpoint:
                self._observer.on_checkpoint(step.name)
            if not effect_seen or not self._is_alive():
                return RunResult(passed=False, completed=tuple(completed), failed_step=step.name)
            completed.append(step.name)
        return RunResult(passed=True, completed=tuple(completed))
