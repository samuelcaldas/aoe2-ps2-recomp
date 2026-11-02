"""Tests for safe early termination in the visual Switch scenario driver."""
import tempfile
import unittest
from pathlib import Path

from tools.drive_switch_scenario import follow_guest_event


class TestFollowGuestEvent(unittest.TestCase):
    def test_observed_marker_finishes_without_sending_more_actions(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.log"
            log.write_bytes(b"guest progress [map-submit] ready")
            called = []
            self.assertTrue(follow_guest_event(log, b"[map-submit]", 1, lambda: called.append(True)))
            self.assertEqual(called, [])

    def test_visual_callback_can_stop_further_input(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "run.log"
            log.write_bytes(b"guest progress without completion marker")
            called = []

            def stop_after_unverified_transition():
                called.append(True)
                return False

            self.assertFalse(follow_guest_event(
                log, b"[map-submit]", 10, stop_after_unverified_transition, max_interval_seconds=0
            ))
            self.assertEqual(called, [True])


if __name__ == "__main__":
    unittest.main()
