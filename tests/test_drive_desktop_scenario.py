import unittest

from tools.drive_desktop_scenario import SCENARIO_NAMES, boot_steps, build_scenario
from tools.scenarios import DESKTOP_MENUS, MenuLayout, Press, Tap, UnresolvedLayoutError

MEASURED = MenuLayout(
    single_player=(458, 85),
    random_map=(458, 124),
    game_settings_neutral=(120, 290),
    game_settings_ok=(258, 408),
    start_game=(293, 409),
)


class DesktopScenarioTableTests(unittest.TestCase):
    def test_scenario_names_are_the_shared_set(self):
        self.assertEqual(SCENARIO_NAMES, ("main-menu", "single-player", "random-map-start", "calibration"))

    def test_campaign_scenarios_are_removed_with_a_clear_message(self):
        for name in ("campaign", "campaign-auto"):
            with self.subTest(name=name), self.assertRaises(ValueError) as raised:
                build_scenario(name, MEASURED)
            self.assertIn("removed", str(raised.exception))
            self.assertIn("random-map-start", str(raised.exception))

    def test_random_map_start_builds_from_the_measured_desktop_layout(self):
        names = [step.name for step in build_scenario("random-map-start", DESKTOP_MENUS)]
        self.assertIn("start-game", names)
        self.assertEqual(names[-1], "map")

    def test_random_map_start_is_boot_followed_by_the_shared_flow(self):
        steps = build_scenario("random-map-start", MEASURED)
        boot_count = len(boot_steps())
        self.assertEqual(steps[boot_count - 1].name, "profile-settle")
        self.assertEqual(steps[boot_count].name, "single-player")
        self.assertEqual(steps[-1].name, "map")

    def test_boot_converts_keys_and_clicks_to_commands(self):
        steps = boot_steps()
        self.assertIsInstance(steps[0], Press)
        self.assertEqual(steps[0].button, "Return")
        self.assertIsInstance(steps[2], Tap)
        self.assertEqual(steps[2].point, (176, 64))

    def test_calibration_appends_pointer_moves_after_the_map(self):
        names = [step.name for step in build_scenario("calibration", MEASURED)]
        self.assertEqual(names[-2:], ["calib-100-120", "calib-520-380"])
        self.assertLess(names.index("map"), names.index("calib-100-120"))

    def test_unknown_scenario_is_rejected(self):
        with self.assertRaises(ValueError):
            build_scenario("does-not-exist", MEASURED)


if __name__ == "__main__":
    unittest.main()
