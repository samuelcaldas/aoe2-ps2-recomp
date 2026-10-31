import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools.drive_switch_scenario import follow_guest_event


class TestFollowGuestEvent(unittest.TestCase):
    def test_actions_are_spaced_when_log_is_at_eof(self):
        clock = [0.0]
        actions = []

        def sleep(seconds):
            clock[0] += seconds

        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.log"
            log.write_bytes(b"boot\n")
            with patch("tools.drive_switch_scenario.time.monotonic", side_effect=lambda: clock[0]), \
                 patch("tools.drive_switch_scenario.time.sleep", side_effect=sleep):
                self.assertFalse(follow_guest_event(
                    log, b"missing", timeout=5, max_interval_seconds=2,
                    on_wait=lambda: actions.append(clock[0]),
                ))
        self.assertEqual(len(actions), 2)
        self.assertGreaterEqual(actions[1] - actions[0], 2)
