import tempfile
import unittest
from pathlib import Path

from tools.drive_desktop_scenario import follow_guest_event


class FakeGame:
    def __init__(self, result=None):
        self.result = result

    def poll(self):
        return self.result


class GuestEventFollowerTests(unittest.TestCase):
    def test_stops_clicking_when_marker_arrives_across_reads(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "game.log"
            path.write_bytes(b"earlier output\n")
            chunks = iter((b"[game-tty] 4 e-", b"txt 50444\n"))
            calls = []

            def advance():
                calls.append(1)
                with path.open("ab") as output:
                    output.write(next(chunks))

            self.assertTrue(follow_guest_event(path, b"[game-tty] 4 e-txt", FakeGame(), 5, advance))
            self.assertEqual(len(calls), 2)

    def test_existing_marker_wins_without_clicks_and_exit_is_reported(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "game.log"
            path.write_bytes(b"[map-submit] frame\n")
            self.assertTrue(follow_guest_event(path, b"[map-submit]", FakeGame(), 0,
                                               lambda: self.fail("unexpected click")))
            self.assertFalse(follow_guest_event(path, b"missing", FakeGame(1), 5,
                                                lambda: self.fail("unexpected click")))


if __name__ == "__main__":
    unittest.main()
