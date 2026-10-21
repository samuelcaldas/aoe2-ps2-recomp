#!/usr/bin/env python3
"""Tests for run-log verdicts: whether the guest is progressing, looping, or blocked."""
import unittest

from tools.classify_diagnostics import RunVerdict, classify_run_log


def tick_lines(pcs: list) -> str:
    return "".join(f"[run:tick] tick={index} pc=0x{pc:08x} ra=0x0 sp=0x0 gp=0x0\n" for index, pc in enumerate(pcs))


MISSING_LINE = "[guest-branch:missing-target] kind=call pc=0x00123456 target=0x00654321\n"


class TestRunVerdict(unittest.TestCase):
    def test_advancing_program_counter_is_progressing(self):
        evidence = classify_run_log(tick_lines([0x100008, 0x100100, 0x100200, 0x100300, 0x100400, 0x100500]))
        self.assertIs(evidence.verdict, RunVerdict.PROGRESSING)
        self.assertEqual(evidence.tick_samples, 6)

    def test_recent_window_stuck_on_few_addresses_is_stalled_in_loop(self):
        pcs = [0x100008, 0x100100] + [0x200000, 0x200004] * 4
        evidence = classify_run_log(tick_lines(pcs))
        self.assertIs(evidence.verdict, RunVerdict.STALLED_IN_LOOP)
        self.assertLessEqual(evidence.recent_distinct_pcs, 3)

    def test_early_stall_followed_by_progress_is_progressing(self):
        pcs = [0x200000] * 8 + [0x300000, 0x300100, 0x300200, 0x300300, 0x300400]
        self.assertIs(classify_run_log(tick_lines(pcs)).verdict, RunVerdict.PROGRESSING)

    def test_missing_target_takes_priority_over_ticks(self):
        evidence = classify_run_log(tick_lines([0x100008, 0x100100, 0x100200]) + MISSING_LINE)
        self.assertIs(evidence.verdict, RunVerdict.MISSING_FUNCTION)
        self.assertEqual(evidence.missing_targets, 1)

    def test_no_ticks_means_no_evidence(self):
        evidence = classify_run_log("Starting execution at address 0x100008\n")
        self.assertIs(evidence.verdict, RunVerdict.NO_EVIDENCE)
        self.assertEqual(evidence.tick_samples, 0)

    def test_single_tick_is_not_enough_to_judge(self):
        self.assertIs(classify_run_log(tick_lines([0x100008])).verdict, RunVerdict.NO_EVIDENCE)

    def test_counts_unserved_rpc_events_by_service_id(self):
        log = "".join("[IOP/RPC trace:unhandled] sid=0x80000211 rpc=0x1 pc=0x1 ra=0x1\n" for _ in range(3))
        log += "[IOP/RPC trace:unhandled] sid=0x80000592 rpc=0x2 pc=0x1 ra=0x1\n"
        evidence = classify_run_log(log)
        self.assertEqual(evidence.unserved_rpc_by_sid, {"0x80000211": 3, "0x80000592": 1})


if __name__ == "__main__":
    unittest.main()
