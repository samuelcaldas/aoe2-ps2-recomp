import inspect
import unittest

from tools.scenarios import (
    CALIBRATION_POINTS,
    DESKTOP_MENUS,
    MAP_MARKER,
    AwaitMarker,
    CheckpointRecorder,
    Hover,
    MenuLayout,
    Tap,
    UnresolvedLayoutError,
    Wait,
    calibration_steps,
    random_map_steps,
)
from pathlib import Path

MEASURED = MenuLayout(
    single_player=(458, 85),
    random_map=(458, 124),
    game_settings_neutral=(120, 290),
    game_settings_ok=(300, 400),
    start_game=(300, 200),
)
EXPECTED_ORDER = (
    "single-player",
    "random-map-hover",
    "random-map",
    "game-settings",
    "game-settings-dismiss",
    "game-settings-ok",
    "player-setup",
    "start-game",
    "loading",
    "map",
)


class RandomMapFlowTests(unittest.TestCase):
    def test_flow_visits_each_menu_in_order(self):
        names = tuple(step.name for step in random_map_steps(MEASURED))
        self.assertEqual(names, EXPECTED_ORDER)

    def test_every_menu_point_comes_from_the_layout(self):
        steps = {step.name: step for step in random_map_steps(MEASURED)}
        self.assertEqual(steps["single-player"].point, MEASURED.single_player)
        self.assertEqual(steps["random-map"].point, MEASURED.random_map)
        self.assertEqual(steps["start-game"].point, MEASURED.start_game)
        self.assertEqual(steps["game-settings-ok"].point, MEASURED.game_settings_ok)
        self.assertEqual(steps["game-settings-dismiss"].point, MEASURED.game_settings_neutral)
        self.assertIsInstance(steps["random-map-hover"], Hover)

    def test_map_is_confirmed_by_the_guest_marker_not_by_timing(self):
        map_step = random_map_steps(MEASURED)[-1]
        self.assertIsInstance(map_step, AwaitMarker)
        self.assertEqual(map_step.marker, MAP_MARKER)
        self.assertEqual(MAP_MARKER, b"[map-submit]")

    def test_desktop_flow_is_fully_measured(self):
        names = [step.name for step in random_map_steps(DESKTOP_MENUS)]
        self.assertEqual(tuple(names), EXPECTED_ORDER)

    def test_flow_refuses_to_build_while_any_point_is_unmeasured(self):
        unmeasured = MenuLayout(
            single_player=(458, 85),
            random_map=(458, 124),
            game_settings_neutral=(120, 290),
            game_settings_ok=(258, 408),
            start_game=None,
        )
        with self.assertRaises(UnresolvedLayoutError):
            random_map_steps(unmeasured)

    def test_flow_does_not_depend_on_the_input_device(self):
        parameters = inspect.signature(random_map_steps).parameters
        self.assertEqual(list(parameters), ["menus"])

    def test_checkpoints_have_recordable_names(self):
        recorder = CheckpointRecorder("pc", lambda name: Path(name))
        for step in random_map_steps(MEASURED):
            if step.checkpoint:
                self.assertTrue(recorder.file_name(step.name).startswith("pc-random-map-"))

    def test_every_flow_step_is_a_checkpoint(self):
        self.assertTrue(all(step.checkpoint for step in random_map_steps(MEASURED)))


class DesktopMeasurementTests(unittest.TestCase):
    """Each logical point must land inside the button box measured in window pixels."""

    WINDOW = (640, 448)
    BOXES = {
        "game_settings_ok": (212, 396, 305, 420),
        "start_game": (245, 400, 342, 420),
    }

    def test_measured_points_land_inside_their_button_boxes(self):
        from tools.presentation_geometry import to_window

        for field, (left, top, right, bottom) in self.BOXES.items():
            with self.subTest(field=field):
                x, y = to_window(getattr(DESKTOP_MENUS, field), self.WINDOW)
                self.assertTrue(left <= x <= right, (field, x))
                self.assertTrue(top <= y <= bottom, (field, y))

    def test_points_are_logical_not_window_pixels(self):
        # The window pixel for OK is 258; logical x is (258 - 40) / 0.875 = 249.
        self.assertEqual(DESKTOP_MENUS.game_settings_ok, (249, 408))
        self.assertEqual(DESKTOP_MENUS.start_game, (289, 409))


class CalibrationTests(unittest.TestCase):
    def test_calibration_moves_the_pointer_to_the_documented_points(self):
        steps = calibration_steps()
        self.assertEqual(tuple(step.point for step in steps), CALIBRATION_POINTS)
        self.assertTrue(all(isinstance(step, Hover) for step in steps))


class PlatformNeutralTests(unittest.TestCase):
    def test_menu_layout_names_every_measured_point(self):
        self.assertEqual(
            {field for field in MenuLayout.__dataclass_fields__},
            {"single_player", "random_map", "game_settings_neutral", "game_settings_ok", "start_game"},
        )

    def test_tap_steps_are_the_only_input_kind_in_the_flow(self):
        kinds = {type(step) for step in random_map_steps(MEASURED)}
        self.assertLessEqual(kinds, {Tap, Hover, Wait, AwaitMarker})


if __name__ == "__main__":
    unittest.main()


class PadFlowTests(unittest.TestCase):
    def test_pad_flow_uses_presses_and_gates_known_screens(self):
        from tools.scenarios import AwaitScreen, Press, random_map_pad_steps

        steps = random_map_pad_steps()
        self.assertEqual(steps[0].name, "main-menu")
        self.assertIsInstance(steps[0], AwaitScreen)
        self.assertEqual(steps[0].state, "main_menu")
        self.assertTrue(all(isinstance(s, (Press, Wait, AwaitScreen, AwaitMarker)) for s in steps))
        self.assertFalse(any(isinstance(s, (Tap, Hover)) for s in steps))

    def test_pad_flow_ends_on_the_map_marker_and_keeps_checkpoints(self):
        from tools.scenarios import random_map_pad_steps

        steps = random_map_pad_steps()
        self.assertEqual(steps[-1].name, "map")
        self.assertEqual(steps[-1].marker, MAP_MARKER)
        self.assertTrue(all(step.checkpoint for step in steps))
