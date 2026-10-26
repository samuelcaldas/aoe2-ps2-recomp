import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class TestNativeGpuEntries(unittest.TestCase):
    def test_entries_file_exists_and_has_valid_format(self):
        entries_path = ROOT / "gpu" / "data" / "gpu_module_entries.txt"
        self.assertTrue(entries_path.exists(), "gpu_module_entries.txt must exist")
        lines = [line.strip() for line in entries_path.read_text().splitlines() if line.strip() and not line.startswith("#")]
        self.assertEqual(len(lines), 7)
        self.assertEqual({int(line.split()[1], 16) for line in lines},
                         {0x125808, 0x125858, 0x1258a8, 0x1258f8, 0x125940, 0x125988, 0x1259c8})
        adapter = (ROOT / "game_hooks/src/gpu_runtime_adapter.hpp").read_text()
        inventory = adapter.split("entries{", 1)[1].split("};", 1)[0]
        self.assertEqual([int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", inventory)],
                         [int(line.split()[1], 16) for line in lines])
        for line in lines:
            parts = line.split()
            self.assertEqual(parts[0], "ENTRY_V")
            self.assertTrue(parts[1].startswith("0x"), f"Invalid address format: {parts[1]}")


if __name__ == "__main__":
    unittest.main()
