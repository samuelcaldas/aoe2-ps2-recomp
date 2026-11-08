"""The shared Single Player → Random Map → Start Game flow, identical on every platform.

Platforms differ only in their InputBackend (pointer, pad, touch); the step list is the same.
"""

from __future__ import annotations

from tools.scenarios.layout import CALIBRATION_POINTS, MenuLayout
from tools.scenarios.steps import AwaitMarker, Hover, Step, Tap, Wait

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
