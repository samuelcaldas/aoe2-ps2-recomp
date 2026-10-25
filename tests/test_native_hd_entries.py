import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class TestNativeHdEntries(unittest.TestCase):
    def test_entries_file_exists_and_has_valid_format(self):
        entries_path = ROOT / "hd" / "data" / "hd_module_entries.txt"
        self.assertTrue(entries_path.exists(), "hd_module_entries.txt must exist")
        lines = [line.strip() for line in entries_path.read_text().splitlines() if line.strip() and not line.startswith("#")]
        self.assertGreaterEqual(len(lines), 20, "Must cover at least 20 hd functions")
        for line in lines:
            parts = line.split()
            self.assertIn(parts[0], {"ENTRY_R", "ENTRY_V", "DEFERRED"})
            self.assertTrue(parts[1].startswith("0x"), f"Invalid address format: {parts[1]}")


if __name__ == "__main__":
    unittest.main()
