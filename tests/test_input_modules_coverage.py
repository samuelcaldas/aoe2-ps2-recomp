"""Coverage map of the kt_mouse / kt_keybd functions for the native input modules.

The classification file game_hooks/src/input_module_entries.txt must cover every function of both modules exactly once. Counts are
asserted even without the decomp checkout; only the comparison against the decomp sources is skipped when it is absent.
"""
import re
import unittest
from pathlib import Path

from tools.trace_addresses import parse_function_addresses

REPO_ROOT = Path(__file__).resolve().parent.parent
ENTRIES = REPO_ROOT / "game_hooks" / "src" / "input_module_entries.txt"
DECOMP = Path.home() / "repos" / "aoe" / "src" / "kcet"
HOOKS_INI = REPO_ROOT / "game" / "SLES-50282" / "kcet_hooks.ini"
KINDS = {"ENTRY", "DEVICE", "DEFERRED"}
TOTALS = {"mouse": 45, "keyboard": 42}
EXPECTED = {
    "mouse": {"ENTRY": 32, "DEVICE": 4, "DEFERRED": 9},
    "keyboard": {"ENTRY": 35, "DEVICE": 0, "DEFERRED": 7},
}
LOCKS = {0x134F90, 0x134FB8, 0x136008, 0x136030}


def load_entries():
    rows = []
    for line in ENTRIES.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        module, kind, address, reason = line.split(" ", 3)
        rows.append((module, kind, int(address, 16), reason))
    return rows


class InputModuleCoverageTest(unittest.TestCase):
    def test_every_row_is_well_formed_and_unique(self):
        rows = load_entries()
        addresses = [(module, address) for module, _, address, _ in rows]
        self.assertEqual(len(addresses), len(set(addresses)), "an address is classified twice")
        for module, kind, address, reason in rows:
            self.assertIn(module, TOTALS)
            self.assertIn(kind, KINDS)
            self.assertEqual(address % 4, 0, hex(address))
            self.assertTrue(reason.strip(), "a row needs its reason")

    def test_counts_per_module_and_kind(self):
        rows = load_entries()
        for module, total in TOTALS.items():
            mine = [r for r in rows if r[0] == module]
            self.assertEqual(len(mine), total, module)
            for kind, expected in EXPECTED[module].items():
                self.assertEqual(sum(1 for r in mine if r[1] == kind), expected, (module, kind))

    def test_locks_are_deferred_with_the_atomic_reason(self):
        for module, kind, address, reason in load_entries():
            if address in LOCKS:
                self.assertEqual(kind, "DEFERRED", hex(address))
                self.assertIn("atomic", reason)

    def test_device_rows_are_the_mouse_rpc_functions_only(self):
        device = {(m, a) for m, k, a, _ in load_entries() if k == "DEVICE"}
        self.assertEqual({m for m, _ in device}, {"mouse"})
        self.assertEqual({a for _, a in device}, {0x135DA0, 0x135DE8, 0x135E30, 0x135E78})

    def test_local_hook_roles_match_the_device_rows_when_resolved(self):
        if not HOOKS_INI.exists():
            self.skipTest("kcet_hooks.ini not generated on this machine")
        roles = dict(line.split("=", 1) for line in HOOKS_INI.read_text().splitlines() if line.startswith("kcet.mouse."))
        mouse_device = {a for m, k, a, _ in load_entries() if k == "DEVICE"}
        self.assertEqual({int(v, 16) for v in roles.values()}, mouse_device)

    def test_function_set_matches_the_decomp_sources(self):
        if not (DECOMP / "kt_mouse.c").exists():
            self.skipTest("decomp checkout not found next to this repository")
        rows = load_entries()
        for module, source in (("mouse", "kt_mouse.c"), ("keyboard", "kt_keybd.c")):
            expected = set(parse_function_addresses((DECOMP / source).read_text()))
            self.assertEqual({a for m, _, a, _ in rows if m == module}, expected, module)


if __name__ == "__main__":
    unittest.main()
