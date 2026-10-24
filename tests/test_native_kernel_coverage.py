import re
import unittest
from pathlib import Path

from tools.trace_addresses import parse_function_addresses

REPO_ROOT = Path(__file__).resolve().parents[1]
DECOMP_SOURCE = Path.home() / "repos" / "aoe" / "src" / "kcet" / "kt_kernel.c"
ENTRY_FILE = REPO_ROOT / "game_hooks" / "src" / "kernel_entries.def"
ENTRY = re.compile(r'^KERNEL_ENTRY\(([^,]+),\s*(0x[0-9A-Fa-f]+)\)$')
DEFERRED = re.compile(r'^KERNEL_DEFERRED\((0x[0-9A-Fa-f]+),\s*"[^"]+"\)$')
ORIGIN = re.compile(r'^/\* Origin: .* \*/$')


class TestNativeKernelCoverage(unittest.TestCase):
    def test_entry_addresses_cover_decomp_exactly(self):
        if not DECOMP_SOURCE.is_file():
            self.skipTest("owned decomp source is unavailable")
        source_addresses = set(parse_function_addresses(DECOMP_SOURCE.read_text(errors="replace")))
        entry_addresses = set()
        names = []
        deferred_addresses = set()
        for line in ENTRY_FILE.read_text().splitlines():
            entry_match = ENTRY.fullmatch(line)
            deferred_match = DEFERRED.fullmatch(line)
            if entry_match:
                names.append(entry_match.group(1))
                entry_addresses.add(int(entry_match.group(2), 16))
            elif deferred_match:
                deferred_addresses.add(int(deferred_match.group(1), 16))
            elif ORIGIN.fullmatch(line):
                continue
            else:
                self.fail(f"invalid kernel entry line: {line}")
        self.assertEqual(len(entry_addresses), 38)
        self.assertEqual(len(deferred_addresses), 1)
        self.assertEqual(len(names), len(set(names)))
        self.assertEqual(entry_addresses | deferred_addresses, source_addresses)
