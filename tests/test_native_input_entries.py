"""Native module entry lists must match the approved input classification."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CLASSIFICATION = ROOT / "game_hooks/src/input_module_entries.txt"
HOOKS = ROOT / "game/SLES-50282/kcet_hooks.ini"
ENTRIES = {
    "mouse": ROOT / "game_hooks/src/kt_mouse_entries.inc",
    "keyboard": ROOT / "game_hooks/src/kt_keybd_entries.inc",
}
HOOK_ROLES = {
    "kcet.mouse.bind", "kcet.mouse.get_info", "kcet.mouse.read", "kcet.mouse.device_info",
    "kcet.keyboard.bind", "kcet.keyboard.get_info", "kcet.keyboard.read", "kcet.keyboard.device_info",
    "kcet.keyboard.set_led", "kcet.keyboard.set_caps_type",
}


class NativeInputEntriesTest(unittest.TestCase):
    def test_entries_and_deferred_addresses_follow_classification_without_devices(self):
        classified = {"mouse": {}, "keyboard": {}}
        for row in CLASSIFICATION.read_text().splitlines():
            if row and not row.startswith("#"):
                module, kind, address, _ = row.split(" ", 3)
                classified[module].setdefault(kind, set()).add(int(address, 16))

        hooks = {}
        if HOOKS.exists():
            hooks = {
                role: int(address, 16)
                for role, address in (line.split("=", 1) for line in HOOKS.read_text().splitlines() if "=" in line)
                if role.startswith(("kcet.mouse.", "kcet.keyboard."))
            }
        device_addresses = set(hooks.values())
        if HOOKS.exists():
            self.assertEqual(set(hooks), HOOK_ROLES)
            self.assertEqual(len(device_addresses), len(HOOK_ROLES))

        for module, path in ENTRIES.items():
            entry_addresses = set()
            deferred_addresses = set()
            for line in path.read_text().splitlines():
                if not line or line.startswith("//"):
                    continue
                match = re.fullmatch(r"(?:ENTRY_[RV]\([^,]+,\s*(0x[0-9A-Fa-f]+),.*\)|DEFERRED\((0x[0-9A-Fa-f]+),.*\))", line)
                self.assertIsNotNone(match, f"malformed entry: {line}")
                address = int(next(value for value in match.groups() if value), 16)
                (deferred_addresses if line.startswith("DEFERRED") else entry_addresses).add(address)

            self.assertEqual(len(entry_addresses), 32 if module == "mouse" else 35)
            self.assertEqual(deferred_addresses, classified[module]["DEFERRED"])
            self.assertEqual(entry_addresses, classified[module]["ENTRY"])
            self.assertFalse(entry_addresses & device_addresses)
            self.assertFalse(deferred_addresses & device_addresses)

    def test_device_hook_addresses_match_classified_device_rows(self):
        if not HOOKS.exists():
            self.skipTest("kcet_hooks.ini not available")
        configured = {
            role: int(address, 16)
            for role, address in (line.split("=", 1) for line in HOOKS.read_text().splitlines() if "=" in line)
            if role.startswith(("kcet.mouse.", "kcet.keyboard."))
        }
        rows = [line.split(" ", 3) for line in CLASSIFICATION.read_text().splitlines() if line and not line.startswith("#")]
        expected = {reason: int(address, 16) for module, kind, address, reason in rows if kind == "DEVICE"}
        role_addresses = {
            "kcet.mouse.bind": expected["kcet.mouse.bind"],
            "kcet.mouse.get_info": expected["kcet.mouse.get_info"],
            "kcet.mouse.read": expected["kcet.mouse.read"],
            "kcet.mouse.device_info": expected["kcet.mouse.device_info"],
        }
        self.assertEqual(set(configured), HOOK_ROLES)
        self.assertEqual({key: configured[key] for key in role_addresses}, role_addresses)
        self.assertTrue(all(configured[role] > 0 for role in HOOK_ROLES))
        self.assertEqual(len(set(configured.values())), len(HOOK_ROLES))


if __name__ == "__main__":
    unittest.main()
