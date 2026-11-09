#!/usr/bin/env python3
"""Tests for the Nintendo Switch emulator validation driver."""

import tempfile
import unittest
from pathlib import Path
from unittest.mock import MagicMock, patch

from tools import validate_switch as validation
from tools import view_switch_novnc


def histogram(counts: dict) -> str:
    return "\n".join(
        f"{count:>10}: ({r},{g},{b}) #000000 x" for (r, g, b), count in counts.items()
    )


class FakeImageMagick:
    """Answers the histogram and compare calls that the frame analyzer makes."""

    def __init__(self, frame_histograms: dict, metric: str = "0"):
        self.frame_histograms = frame_histograms
        self.metric = metric

    def __call__(self, args):
        if args[1] == "compare":
            return self.metric
        return self.frame_histograms[Path(args[1]).name]


REAL_COLORS = {(0, 0, 0): 1000, **{(index * 3, 90, 200): 50 for index in range(40)}}
FALLBACK_COLORS = {(0, 0, 0): 1000, (255, 0, 255): 60000}
PROGRESSING_LOG = "".join(
    f"ps2x: [run:tick] tick={i} pc=0x{0x100008 + 0x40 * i:08x}\n" for i in range(6)
)


class TestSwitchValidationScript(unittest.TestCase):
    def setUp(self):
        self.script = validation.emulator_script(run_seconds=45)

    def test_starts_virtual_display_and_headless_emulator(self):
        self.assertIn("Xvfb :99", self.script)
        self.assertIn("DISPLAY=:99", self.script)
        self.assertIn("Ryujinx.Headless.SDL2", self.script)
        self.assertIn("--root-data-dir", self.script)

    def test_copies_keys_if_provided(self):
        self.assertIn("prod.keys", self.script)

    def test_memory_cards_use_persistent_non_overwriting_sdcard(self):
        self.assertIn("persistent memory cards", self.script)
        self.assertIn(
            'if [ ! -e "/src/build/switch-validation/ryujinx-data/sdcard/switch/aoe2/$name" ]',
            self.script,
        )
        self.assertIn(
            "mkdir -p /src/build/switch-validation/ryujinx-data/sdcard/switch/aoe2/mc0 "
            "/src/build/switch-validation/ryujinx-data/sdcard/switch/aoe2/mc1",
            self.script,
        )

    def test_cleanup_preserves_memory_cards_and_removes_transient_ryujinx_data(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "ryujinx-data"
            card = root / "sdcard" / "switch" / "aoe2" / "mc0" / "PLAYER.psu"
            config = root / "Config.json"
            card.parent.mkdir(parents=True)
            card.write_bytes(b"saved-card")
            config.write_text("transient")

            validation.clean_ryujinx_transient_data(root)

            self.assertEqual(card.read_bytes(), b"saved-card")
            self.assertFalse(config.exists())

    def test_manual_view_loads_the_persistent_cards_and_configures_input(self):
        script = view_switch_novnc.novnc_runner_script()
        self.assertIn("persistent memory cards", script)
        self.assertIn(
            "--root-data-dir /src/build/switch-validation/ryujinx-data", script
        )
        self.assertIn("cfg['enable_keyboard'] = True", script)
        self.assertIn("cfg['enable_mouse'] = True", script)
        self.assertIn("export AOE2_LOG_POINTER=1", script)
        self.assertIn("/tools/novnc_mobile/mobile.js", script)
        self.assertIn("http://localhost:6080/mobile/", script)
        self.assertIn("manual-run.log", script)

    def test_mobile_view_has_touch_mouse_gestures_and_soft_keyboard(self):
        mobile = Path(__file__).resolve().parents[1] / "tools" / "novnc_mobile"
        page = (mobile / "index.html").read_text(encoding="utf-8")
        script = (mobile / "mobile.js").read_text(encoding="utf-8")
        self.assertIn("id=\"keyboard-toggle\"", page)
        self.assertIn("id=\"text-entry\"", page)
        self.assertIn("touchstart", script)
        self.assertIn("touchmove", script)
        self.assertIn("500", script)
        self.assertIn("rfb.sendKey", script)

    def test_manual_view_accepts_separate_build_artifacts(self):
        script = view_switch_novnc.novnc_runner_script(
            "build/docker-switch-release/aoe2_game.nro"
        )
        self.assertIn("/src/build/docker-switch-release/aoe2_game.nro", script)

    def test_collects_screenshots_and_checks_liveness(self):
        self.assertIn("xwd -root", self.script)
        self.assertIn(".playwright-mcp/switch", self.script)
        self.assertIn("alive.txt", self.script)
        self.assertIn("run.log", self.script)

    def test_screenshot_dir_points_to_switch_subfolder(self):
        self.assertEqual(validation.SCREENSHOT_DIR, ".playwright-mcp/switch")

    def test_container_command_options(self):
        command = validation.container_command(
            run_seconds=60,
            jobs=4,
            nro="build/docker-switch-release/aoe2_game.nro",
        )
        self.assertIn("--memory=12g", command)
        self.assertIn("--cpus=4", command)
        self.assertIn("aoe2-switch-emulator", command)
        self.assertIn("--name=aoe2-validate-switch", command)
        self.assertIn("--hostname=aoe2-validate-switch", command)
        self.assertIn("--label=aoe2.platform=switch", command)
        self.assertIn("--label=aoe2.role=validate", command)
        self.assertIn("/src/build/docker-switch-release/aoe2_game.nro", command[-1])

    def test_default_jobs_is_eight(self):
        self.assertEqual(validation.DEFAULT_JOBS, 8)
        command = validation.container_command(run_seconds=60)
        self.assertIn("--cpus=8", command)

    def test_verdict_passes_only_with_progress_and_changing_real_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [
                str(Path(directory) / f"screen-native-{index}.png") for index in (1, 2)
            ]
            magick = FakeImageMagick(
                {
                    "screen-native-1.png": histogram(REAL_COLORS),
                    "screen-native-2.png": histogram(REAL_COLORS),
                },
                metric="5000",
            )
            self.assertTrue(
                validation.evidence_passed(True, PROGRESSING_LOG, shots, magick)
            )
            self.assertFalse(
                validation.evidence_passed(False, PROGRESSING_LOG, shots, magick)
            )
            self.assertFalse(validation.evidence_passed(True, "", shots, magick))

    def test_fallback_screenshots_fail_even_when_the_process_is_alive(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [
                str(Path(directory) / f"screen-native-{index}.png") for index in (1, 2)
            ]
            magick = FakeImageMagick(
                {
                    "screen-native-1.png": histogram(FALLBACK_COLORS),
                    "screen-native-2.png": histogram(FALLBACK_COLORS),
                }
            )
            self.assertFalse(
                validation.evidence_passed(True, PROGRESSING_LOG, shots, magick)
            )

    def test_preflight_fails_fast_on_missing_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            fake_keys = Path(directory) / "prod.keys"
            with patch.object(validation, "NRO_PATH", "nonexistent/aoe2_game.nro"):
                with self.assertRaises(validation.PreflightError):
                    validation.check_preflight(keys_path=fake_keys)

    def test_preflight_allows_missing_keys_for_homebrew(self):
        with tempfile.TemporaryDirectory() as directory:
            fake_keys = Path(directory) / "nonexistent.keys"
            with patch.object(validation, "NRO_PATH", "tools/validate_switch.py"):
                # Should not raise PreflightError when binary exists, even if keys are absent
                validation.check_preflight(keys_path=fake_keys)

    def test_emulator_script_mode_tagging_and_configuration(self):
        script_native = validation.emulator_script(run_seconds=60, mode="native")
        self.assertIn("mode=native", script_native)
        self.assertIn("export AOE2_MAP_VIEW=native", script_native)
        self.assertIn("screen-native-", script_native)
        self.assertIn("tools/drive_switch_scenario.py --mode native", script_native)

        script_gs = validation.emulator_script(run_seconds=60, mode="gs")
        self.assertIn("mode=legacy", script_gs)
        self.assertIn("export AOE2_MAP_VIEW=legacy", script_gs)
        self.assertIn("screen-gs-", script_gs)
        self.assertIn("tools/drive_switch_scenario.py --mode gs", script_gs)

    def test_native_mode_fails_if_prohibited_gs_fallback_occurs(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [str(Path(directory) / f"screen-native-{i}.png") for i in (1, 2)]
            magick = FakeImageMagick(
                {
                    "screen-native-1.png": histogram(REAL_COLORS),
                    "screen-native-2.png": histogram(REAL_COLORS),
                },
                metric="5000",
            )
            failing_log = (
                PROGRESSING_LOG
                + "Native map view snapshot failed; runtime fallback to GS is prohibited in native mode\n"
            )
            self.assertFalse(
                validation.evidence_passed(
                    True, failing_log, shots, magick, mode="native"
                )
            )

    def test_random_map_start_never_reports_an_automated_map_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [
                str(Path(directory) / f"screen-native-{index}.png") for index in (1, 2)
            ]
            magick = FakeImageMagick(
                {
                    "screen-native-1.png": histogram(REAL_COLORS),
                    "screen-native-2.png": histogram(REAL_COLORS),
                },
                metric="5000",
            )
            # Progressing log without [map-submit] and screenshots without map in filename
            self.assertFalse(
                validation.evidence_passed(
                    True, PROGRESSING_LOG, shots, magick, scenario="random-map-start"
                )
            )

            # A map marker and changing frames do not prove a visible map.
            # Acceptance is visual, so the automated verdict stays False even with [map-submit].
            map_log = PROGRESSING_LOG + "[map-submit] call=1 area=0x1000\n"
            self.assertFalse(
                validation.evidence_passed(
                    True, map_log, shots, magick, scenario="random-map-start"
                )
            )

            # Prohibited GS fallback fails even if [map-submit] was reached
            prohibited_fallback_log = (
                map_log
                + "Native map view snapshot failed; runtime fallback to GS is prohibited in native mode\n"
            )
            self.assertFalse(
                validation.evidence_passed(
                    True,
                    prohibited_fallback_log,
                    shots,
                    magick,
                    mode="native",
                    scenario="random-map-start",
                )
            )

    def test_removed_campaign_scenarios_are_rejected_by_the_validator(self):
        for name in ("campaign", "campaign-auto"):
            with self.subTest(name=name):
                self.assertIn(name, validation.REMOVED_SCENARIOS)
                self.assertNotIn(name, validation.SCENARIO_CHOICES)
                self.assertEqual(validation.REMOVED_SCENARIOS[name], "use random-map-start")

    def test_native_rejects_observed_guest_framebuffer_and_gs_paths(self):
        shots = [f"screen-native-{index}.png" for index in (1, 2)]
        magick = FakeImageMagick(
            {name: histogram(REAL_COLORS) for name in shots}, metric="5000"
        )
        for marker in (
            "presentation: guest framebuffer fallback",
            "GS::latchHostPresentationFrame",
            "map-only: menus and HUD are not implemented",
        ):
            self.assertFalse(
                validation.evidence_passed(
                    True, PROGRESSING_LOG + marker, shots, magick
                )
            )

    def test_rejects_screenshots_without_mode_tagging(self):
        with tempfile.TemporaryDirectory() as directory:
            shots = [str(Path(directory) / f"screen-{index}.png") for index in (1, 2)]
            magick = FakeImageMagick(
                {
                    "screen-1.png": histogram(REAL_COLORS),
                    "screen-2.png": histogram(REAL_COLORS),
                },
                metric="5000",
            )
            # screen-1.png does not contain -native-
            self.assertFalse(
                validation.evidence_passed(
                    True, PROGRESSING_LOG, shots, magick, mode="native"
                )
            )
            # screen-1.png does not contain -gs-
            self.assertFalse(
                validation.evidence_passed(
                    True, PROGRESSING_LOG, shots, magick, mode="gs"
                )
            )


if __name__ == "__main__":
    unittest.main()
