"""Observer that names and captures one screenshot per scenario checkpoint."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Callable

PLATFORMS = ("pc", "switch", "android")
SCENARIO_NAME = "random-map"
_SAFE_STEP = re.compile(r"^[a-z0-9]+(-[a-z0-9]+)*$")

# Writes a screenshot file named as requested and returns its real path.
Shooter = Callable[[str], Path]


class CheckpointRecorder:
    """Writes ``<platform>-random-map-<step>.png`` for each checkpoint, in step order."""

    def __init__(self, platform: str, shoot: Shooter) -> None:
        if platform not in PLATFORMS:
            raise ValueError(f"unknown platform {platform!r}; expected one of {PLATFORMS}")
        self._platform = platform
        self._shoot = shoot
        self._captured: list[Path] = []

    @property
    def captured(self) -> tuple[Path, ...]:
        return tuple(self._captured)

    def file_name(self, step_name: str) -> str:
        if not _SAFE_STEP.match(step_name):
            raise ValueError(f"checkpoint name must be kebab-case: {step_name!r}")
        return f"{self._platform}-{SCENARIO_NAME}-{step_name}.png"

    def on_checkpoint(self, step_name: str) -> None:
        self._captured.append(self._shoot(self.file_name(step_name)))
