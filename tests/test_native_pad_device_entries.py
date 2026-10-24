import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
DECOMP = Path('/home/samuelcaldas/repos/aoe/src/kcet/kt_libpad.c')
EXPECTED = {
    'init': 0x10CCB0, 'start': 0x10DEC0, 'slot_max': 0x10DDF0,
    'port_open': 0x10CF10, 'port_close': 0x10D0F8, 'state': 0x10D2E0,
    'read': 0x10D260, 'req_state': 0x10D3F8, 'info_act': 0x10D480,
    'info_mode': 0x10D6C0, 'set_main_mode': 0x10D7F8,
    'set_act_direct': 0x10D8B0, 'set_act_align': 0x10D968,
    'info_press_mode': 0x10DBA8,
}


@unittest.skipUnless(DECOMP.is_file(), 'owner decomp not available')
class TestNativePadDeviceEntries(unittest.TestCase):
    def test_entries_are_complete_unique_aligned_and_called_from_decomp(self):
        entries_path = REPO_ROOT / 'game_hooks/src/pad_device_entries.def'
        self.assertTrue(entries_path.is_file(), 'pad device entry table is missing')
        entries = re.findall(r'^PAD_DEVICE_ENTRY\((\w+),\s*(0x[0-9A-Fa-f]+)\)$',
                             entries_path.read_text(), re.MULTILINE)
        self.assertEqual(len(entries), 14)
        addresses = [int(address, 16) for _, address in entries]
        self.assertEqual(set(addresses), set(EXPECTED.values()))
        self.assertEqual(len(addresses), len(set(addresses)))
        self.assertTrue(all(address % 4 == 0 for address in addresses))
        self.assertEqual({name: int(address, 16) for name, address in entries}, EXPECTED)

        source = DECOMP.read_text()
        symbols = {
            'init': 'func_10CCB0', 'start': 'func_10DEC0',
            'slot_max': 'func_10DDF0', 'port_open': 'func_10CF10',
            'port_close': 'func_10D0F8', 'state': 'func_10D2E0',
            'read': 'func_10D260', 'req_state': 'func_10D3F8',
            'info_act': 'func_10D480', 'info_mode': 'func_10D6C0',
            'set_main_mode': 'func_10D7F8', 'set_act_direct': 'func_10D8B0',
            'set_act_align': 'func_10D968', 'info_press_mode': 'func_10DBA8',
        }
        for name, symbol in symbols.items():
            self.assertRegex(source, rf'\b{symbol}\s*\(',
                             f'{name} address has no decomp declaration/call site')
