"""Entry coverage is separate from behavioral parity."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DECOMP = ROOT.parent / 'aoe/src/kcet/kt_libpad.c'
# Four binary-only module helpers documented in pad-libpad-semantics.md.
BINARY_ONLY = {0x125A70, 0x125AA8, 0x125AC8, 0x125AE8}


def entries():
    source = (ROOT / 'game_hooks/src/kt_libpad_entries.inc').read_text()
    active = re.findall(r'^LIBPAD_[RV]\((\w+),\s*(0x[0-9A-Fa-f]+),', source, re.M)
    deferred = re.findall(r'^LIBPAD_DEFERRED\((0x[0-9A-Fa-f]+),\s*"([^"]+)"\)', source, re.M)
    return active, deferred


class TestNativeKtLibpadCoverage(unittest.TestCase):
    def test_unique_names_addresses_and_alignment(self):
        active, deferred = entries()
        self.assertEqual(len(active), 97)
        self.assertEqual(len(deferred), 8)
        self.assertEqual(len(active) + len(deferred), 105)
        names = [name for name, _ in active]
        addresses = [int(address, 16) for _, address in active]
        addresses += [int(address, 16) for address, _ in deferred]
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(len(addresses), len(set(addresses)))
        self.assertTrue(all(address % 4 == 0 for address in addresses))
        self.assertTrue(all(re.fullmatch(r'[A-Z][A-Za-z0-9]*', name) for name in names))
        self.assertTrue(all(reason.strip() for _, reason in deferred))

    @unittest.skipUnless(DECOMP.is_file(), 'owner decomp not available')
    def test_entries_and_deferred_cover_actual_definitions(self):
        source = DECOMP.read_text()
        # Require a body, so prototypes and call sites cannot manufacture coverage.
        definitions = {int(address, 16) for address in re.findall(
            r'\bfunc_([0-9A-Fa-f]{8})\s*\([^;{}]*\)\s*\{', source)}
        active, deferred = entries()
        addresses = {int(address, 16) for _, address in active}
        addresses |= {int(address, 16) for address, _ in deferred}
        self.assertTrue(definitions)
        self.assertEqual(addresses, definitions | BINARY_ONLY)
