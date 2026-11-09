"""The shared Single Player → Random Map → Start Game flow, identical on every platform.

Platforms differ only in their InputBackend (pointer, pad, touch); the step list is the same.
"""

from __future__ import annotations

from tools.scenarios.layout import CALIBRATION_POINTS, MenuLayout
from tools.scenarios.steps import AwaitMarker, AwaitScreen, Hover, Press, Step, Tap, Wait

# Printed by the guest's diagnostic TTY when the map is submitted to the renderer.
MAP_MARKER = b"[map-submit]"
MAP_TIMEOUT_SECONDS = 1200.0


def random_map_steps(menus: MenuLayout) -> tuple[Step, ...]:
    """Main menu → Single Player → Random Map → Game Settings → player setup → Start Game → map.

    The neutral-parchment click before OK closes a Map Type list that was left open in a measured
    run (see docs/validation-scenarios.md); it is harmless otherwise. OK opens the player setup
    screen, whose Start Game button begins the map. Each checkpoint is a screenshot after the step
    settles, so the visual review can confirm every screen the flow passes through.
    """
    return (
        Tap(name="single-player", point=menus.single_player, settle_seconds=10, checkpoint=True),
        Hover(name="random-map-hover", point=menus.random_map, settle_seconds=2, checkpoint=True),
        Tap(name="random-map", point=menus.random_map, settle_seconds=15, checkpoint=True),
        Wait(name="game-settings", settle_seconds=3, checkpoint=True),
        Tap(name="game-settings-dismiss", point=menus.game_settings_neutral, settle_seconds=3, checkpoint=True),
        Tap(name="game-settings-ok", point=menus.game_settings_ok, settle_seconds=15, checkpoint=True),
        Wait(name="player-setup", settle_seconds=3, checkpoint=True),
        Tap(name="start-game", point=menus.start_game, settle_seconds=15, checkpoint=True),
        Wait(name="loading", settle_seconds=30, checkpoint=True),
        AwaitMarker(
            name="map",
            marker=MAP_MARKER,
            timeout_seconds=MAP_TIMEOUT_SECONDS,
            checkpoint=True,
        ),
    )


def calibration_steps() -> tuple[Step, ...]:
    """Pointer moves on the map, for tools/check_pointer_calibration.py (run with AOE2_LOG_POINTER=1)."""
    return tuple(
        Hover(name=f"calib-{x}-{y}", point=(x, y), settle_seconds=4)
        for x, y in CALIBRATION_POINTS
    )


# Game Settings focus order for the pad: Difficulty is focused on entry, and OK is the last
# row. Each press gets a checkpoint so the first live run shows where the focus actually goes.
GAME_SETTINGS_DOWN_PRESSES = 9


def random_map_pad_steps() -> tuple[Step, ...]:
    """Joystick flow for Switch: D-pad moves focus, Cross selects. No pointer input.

    Screens with a detector (main menu, Single Player submenu) are gated by AwaitScreen. Others
    rely on the settle time and checkpoints for the visual review.
    """
    focus_steps = tuple(
        Press(
            name=f"settings-focus-{index}",
            button="down",
            settle_seconds=1,
            checkpoint=True,
        )
        for index in range(1, GAME_SETTINGS_DOWN_PRESSES + 1)
    )
    return (
        AwaitScreen(name="main-menu", state="main_menu", timeout_seconds=90, checkpoint=True),
        Press(name="single-player", button="cross", settle_seconds=5, checkpoint=True),
        AwaitScreen(name="single-player-menu", state="single_player_menu", timeout_seconds=30, checkpoint=True),
        Press(name="random-map-hover", button="down", settle_seconds=2, checkpoint=True),
        Press(name="random-map", button="cross", settle_seconds=15, checkpoint=True),
        Wait(name="game-settings", settle_seconds=3, checkpoint=True),
    ) + focus_steps + (
        Press(name="game-settings-ok", button="cross", settle_seconds=15, checkpoint=True),
        Wait(name="player-setup", settle_seconds=3, checkpoint=True),
        Press(name="start-game", button="cross", settle_seconds=15, checkpoint=True),
        Wait(name="loading", settle_seconds=30, checkpoint=True),
        AwaitMarker(name="map", marker=MAP_MARKER, timeout_seconds=MAP_TIMEOUT_SECONDS, checkpoint=True),
    )
