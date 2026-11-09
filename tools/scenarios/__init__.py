"""Shared validation scenarios: steps (commands), runner (template), backends (strategies), observers."""

from tools.scenarios.backends import RyujinxPadBackend, UnsupportedInputError, XdotoolMouseBackend
from tools.scenarios.layout import CALIBRATION_POINTS, DESKTOP_MENUS, MenuLayout
from tools.scenarios.random_map import MAP_MARKER, calibration_steps, random_map_pad_steps, random_map_steps
from tools.scenarios.recorder import CheckpointRecorder
from tools.scenarios.runner import RunResult, ScenarioRunner
from tools.scenarios.steps import (
    AwaitMarker,
    AwaitScreen,
    Hover,
    Press,
    Step,
    Tap,
    UnresolvedLayoutError,
    Wait,
)
from tools.scenarios.watcher import LogMarkerWatcher

__all__ = [
    "AwaitMarker",
    "AwaitScreen",
    "RyujinxPadBackend",
    "UnsupportedInputError",
    "random_map_pad_steps",
    "CALIBRATION_POINTS",
    "CheckpointRecorder",
    "DESKTOP_MENUS",
    "Hover",
    "LogMarkerWatcher",
    "MAP_MARKER",
    "MenuLayout",
    "Press",
    "RunResult",
    "ScenarioRunner",
    "Step",
    "Tap",
    "UnresolvedLayoutError",
    "Wait",
    "XdotoolMouseBackend",
    "calibration_steps",
    "random_map_steps",
]
