import tempfile
import unittest
from pathlib import Path

from tools.scenarios import (
    AwaitMarker,
    CheckpointRecorder,
    Hover,
    LogMarkerWatcher,
    Press,
    ScenarioRunner,
    Tap,
    UnresolvedLayoutError,
    Wait,
    XdotoolMouseBackend,
)


class RecordingBackend:
    def __init__(self):
        self.actions = []

    def tap(self, point):
        self.actions.append(("tap", point))

    def hover(self, point):
        self.actions.append(("hover", point))

    def press(self, button):
        self.actions.append(("press", button))


class StepValidationTests(unittest.TestCase):
    def test_step_names_must_be_kebab_case(self):
        for bad in ("", "Single Player", "random_map", "-leading", "trailing-", "Upper", "double--dash"):
            with self.subTest(name=bad), self.assertRaises(ValueError):
                Wait(name=bad)

    def test_negative_settle_is_rejected(self):
        with self.assertRaises(ValueError):
            Wait(name="loading", settle_seconds=-1)

    def test_tap_without_measured_point_refuses_to_build(self):
        with self.assertRaises(UnresolvedLayoutError):
            Tap(name="start-game", point=None)

    def test_tap_rejects_negative_or_malformed_points(self):
        with self.assertRaises(ValueError):
            Tap(name="bad", point=(-1, 4))
        with self.assertRaises(ValueError):
            Hover(name="bad", point=(1, 2, 3))

    def test_marker_and_timeout_are_required(self):
        with self.assertRaises(ValueError):
            AwaitMarker(name="map", marker=b"", timeout_seconds=5)
        with self.assertRaises(ValueError):
            AwaitMarker(name="map", marker=b"x", timeout_seconds=0)

    def test_steps_are_immutable(self):
        step = Wait(name="loading")
        with self.assertRaises(AttributeError):
            step.name = "other"


class RunnerTests(unittest.TestCase):
    def make_runner(self, backend, marker_result=True, alive=lambda: True, sleeps=None, scale=1.0):
        sleeps = [] if sleeps is None else sleeps
        calls = []

        def await_marker(marker, timeout):
            calls.append((marker, timeout))
            return marker_result

        runner = ScenarioRunner(
            backend=backend,
            await_marker=await_marker,
            is_alive=alive,
            settle_scale=scale,
            sleep=sleeps.append,
        )
        return runner, calls

    def test_runs_steps_in_order_and_reports_completion(self):
        backend = RecordingBackend()
        runner, _ = self.make_runner(backend)
        result = runner.run((
            Tap(name="single-player", point=(458, 85)),
            Press(name="confirm", button="Return"),
            Wait(name="loading"),
        ))
        self.assertTrue(result.passed)
        self.assertEqual(result.completed, ("single-player", "confirm", "loading"))
        self.assertEqual(backend.actions, [("tap", (458, 85)), ("press", "Return")])

    def test_stops_at_first_unobserved_marker(self):
        backend = RecordingBackend()
        runner, calls = self.make_runner(backend, marker_result=False)
        result = runner.run((
            AwaitMarker(name="map", marker=b"[map-submit]", timeout_seconds=30),
            Wait(name="never-reached"),
        ))
        self.assertFalse(result.passed)
        self.assertEqual(result.failed_step, "map")
        self.assertEqual(result.completed, ())
        self.assertEqual(calls, [(b"[map-submit]", 30)])

    def test_stops_when_guest_exits(self):
        backend = RecordingBackend()
        runner, _ = self.make_runner(backend, alive=lambda: False)
        result = runner.run((Tap(name="start-game", point=(1, 1)), Wait(name="after")))
        self.assertFalse(result.passed)
        self.assertEqual(result.failed_step, "start-game")
        self.assertEqual(len(backend.actions), 1)

    def test_settle_and_timeouts_scale_for_debug_builds(self):
        backend = RecordingBackend()
        sleeps = []
        runner, calls = self.make_runner(backend, sleeps=sleeps, scale=10)
        runner.run((
            Wait(name="loading", settle_seconds=3),
            AwaitMarker(name="map", marker=b"m", timeout_seconds=20),
        ))
        self.assertEqual(sleeps, [30])
        self.assertEqual(calls, [(b"m", 200)])

    def test_checkpoints_fire_after_settle_in_step_order(self):
        class Observer:
            def __init__(self):
                self.names = []

            def on_checkpoint(self, name):
                self.names.append(name)

        observer = Observer()
        backend = RecordingBackend()
        sleeps = []
        runner = ScenarioRunner(
            backend=backend,
            await_marker=lambda marker, timeout: True,
            is_alive=lambda: True,
            observer=observer,
            sleep=sleeps.append,
        )
        runner.run((
            Wait(name="a", settle_seconds=1, checkpoint=True),
            Wait(name="b", settle_seconds=2),
            Wait(name="c", settle_seconds=3, checkpoint=True),
        ))
        self.assertEqual(observer.names, ["a", "c"])
        self.assertEqual(sleeps, [1, 2, 3])

    def test_boot_hook_runs_before_steps(self):
        events = []

        class BootingRunner(ScenarioRunner):
            def boot(self):
                events.append("boot")

        runner = BootingRunner(
            backend=RecordingBackend(),
            await_marker=lambda marker, timeout: True,
            is_alive=lambda: True,
            sleep=lambda seconds: events.append("sleep"),
        )
        runner.run((Wait(name="only", settle_seconds=0.5),))
        self.assertEqual(events, ["boot", "sleep"])


class LogMarkerWatcherTests(unittest.TestCase):
    def test_marker_split_across_reads_is_found(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_bytes(b"booting\n[map-su")
            def guest_writes_rest(seconds):
                with path.open("ab") as log:
                    log.write(b"bmit] ok\n")

            watcher = LogMarkerWatcher(path, lambda: True, sleep=guest_writes_rest)
            self.assertTrue(watcher.await_marker(b"[map-submit]", 5))

    def test_existing_marker_returns_without_waiting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_bytes(b"[map-submit] frame\n")
            watcher = LogMarkerWatcher(path, lambda: True, sleep=lambda s: self.fail("should not sleep"))
            self.assertTrue(watcher.await_marker(b"[map-submit]", 5))

    def test_timeout_returns_false(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_bytes(b"quiet\n")
            now = [0.0]

            def clock():
                return now[0]

            def sleep(seconds):
                now[0] += seconds

            watcher = LogMarkerWatcher(path, lambda: True, clock=clock, sleep=sleep)
            self.assertFalse(watcher.await_marker(b"[map-submit]", 3))

    def test_guest_exit_returns_false_without_waiting_for_timeout(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.log"
            path.write_bytes(b"quiet\n")
            watcher = LogMarkerWatcher(path, lambda: False, sleep=lambda s: self.fail("should not sleep"))
            self.assertFalse(watcher.await_marker(b"[map-submit]", 600))

    def test_missing_log_returns_false_when_guest_already_exited(self):
        with tempfile.TemporaryDirectory() as directory:
            watcher = LogMarkerWatcher(Path(directory) / "absent.log", lambda: False)
            self.assertFalse(watcher.await_marker(b"x", 5))


class RecorderTests(unittest.TestCase):
    def test_names_checkpoints_by_platform_and_step(self):
        shot = []

        def shoot(name):
            shot.append(name)
            return Path("/shots") / name

        recorder = CheckpointRecorder("pc", shoot)
        recorder.on_checkpoint("game-settings")
        self.assertEqual(shot, ["pc-random-map-game-settings.png"])
        self.assertEqual(recorder.captured, (Path("/shots/pc-random-map-game-settings.png"),))

    def test_rejects_unknown_platform_and_unsafe_step_names(self):
        with self.assertRaises(ValueError):
            CheckpointRecorder("ps4", lambda name: Path(name))
        recorder = CheckpointRecorder("pc", lambda name: Path(name))
        with self.assertRaises(ValueError):
            recorder.file_name("../escape")


class XdotoolBackendTests(unittest.TestCase):
    def test_tap_maps_logical_point_and_presses_with_original_timings(self):
        commands, sleeps = [], []
        backend = XdotoolMouseBackend(commands.append, "0x42", (640, 512), sleep=sleeps.append)
        backend.tap((458, 85))
        self.assertEqual(commands, [
            ["xdotool", "mousemove", "--window", "0x42", "458", "97"],
            ["xdotool", "mousedown", "1"],
            ["xdotool", "mouseup", "1"],
        ])
        self.assertEqual(sleeps, [1.5, 0.8])

    def test_window_scaling_follows_presentation_geometry(self):
        # The 640x448 logical screen fills the 640x512 frame, so y scales by 512/448.
        backend = XdotoolMouseBackend(lambda c: None, "0x1", (1280, 1024))
        self.assertEqual(backend.window_point((0, 0)), (0, 0))
        self.assertEqual(backend.window_point((640, 448)), (1280, 1024))

    def test_letterboxed_window_offsets_the_frame(self):
        # 640x448 fits the 640x512 frame at 0.875 scale, leaving a 40px horizontal bar.
        backend = XdotoolMouseBackend(lambda c: None, "0x1", (640, 448))
        self.assertEqual(backend.window_point((0, 0)), (40, 0))

    def test_hover_moves_without_clicking(self):
        commands = []
        backend = XdotoolMouseBackend(commands.append, "0x1", (640, 448), sleep=lambda s: None)
        backend.hover((458, 124))
        self.assertEqual(len(commands), 1)
        self.assertEqual(commands[0][1], "mousemove")

    def test_press_holds_the_key(self):
        commands, sleeps = [], []
        backend = XdotoolMouseBackend(commands.append, "0x1", (640, 448), sleep=sleeps.append)
        backend.press("Return")
        self.assertEqual(commands, [["xdotool", "keydown", "Return"], ["xdotool", "keyup", "Return"]])
        self.assertEqual(sleeps, [0.3])

    def test_requires_a_window(self):
        with self.assertRaises(ValueError):
            XdotoolMouseBackend(lambda c: None, "", (640, 448))


if __name__ == "__main__":
    unittest.main()
