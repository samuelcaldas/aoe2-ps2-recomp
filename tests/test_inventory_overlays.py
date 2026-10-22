import hashlib
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

from tools.inventory_overlays import InventoryError, inventory, main, require_ignored, reservations


def elf_image(headers=None):
    headers = headers if headers is not None else [(1, 0, 4096, 0, 0, 16, 7, 16)]
    raw = bytearray(52 + 32 * len(headers))
    raw[:7] = b'\x7fELF\x01\x01\x01'
    struct.pack_into('<HI', raw, 18, 8, 1)
    struct.pack_into('<I', raw, 28, 52)
    struct.pack_into('<HH', raw, 42, 32, len(headers))
    for index, header in enumerate(headers):
        struct.pack_into('<8I', raw, 52 + 32 * index, *header)
    return bytes(raw)


class OverlayInventoryTests(unittest.TestCase):
    def test_filters_data_and_empty_segments_without_collapsing_shared_addresses(self):
        headers = [(1, 0, 4096, 0, 0, n, f, 16) for n, f in [(16, 7), (32, 7), (16, 6), (0, 7)]]
        self.assertEqual([x['memory_bytes'] for x in reservations(elf_image(headers))], [16, 32])

    def test_rejects_malformed_elf(self):
        raw = elf_image()
        for candidate in (b'', raw[:51], raw[:-1], raw[:4] + b'\x02' + raw[5:]):
            with self.subTest(length=len(candidate)), self.assertRaises(InventoryError):
                reservations(candidate)

    def test_rejects_invalid_segment_ranges(self):
        for header in [(1, 0, 0, 0, 20, 16, 7, 16), (1, 1000, 0, 0, 16, 16, 7, 16),
                       (1, 0, 2**32 - 8, 0, 0, 16, 7, 16)]:
            with self.assertRaises(InventoryError):
                reservations(elf_image([header]))

    def test_inventory_is_evidence_not_verified_mapping(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            raw = elf_image()
            elf = root / 'input.elf'
            elf.write_bytes(raw)
            (root / 'one.BIN').write_bytes(bytes(16))
            (root / 'two.BIN').write_bytes(bytes(16))
            (root / 'short.BIN').write_bytes(bytes(8))
            report = inventory(elf, root, hashlib.sha1(raw).hexdigest())
            self.assertFalse(report['mapping_verified'])
            self.assertEqual(len(report['reservations'][0]['size_candidates']), 2)
            with self.assertRaises(InventoryError):
                inventory(elf, root)
            (root / 'link.BIN').symlink_to(root / 'one.BIN')
            with self.assertRaises(InventoryError):
                inventory(elf, root, hashlib.sha1(raw).hexdigest())

    def test_report_requires_ignored_location(self):
        with self.assertRaises(InventoryError):
            require_ignored(Path(__file__))
        require_ignored(Path(__file__).resolve().parents[1] / 'analysis/generated/overlay_inventory.json')

    def test_failure_does_not_write_report(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp) / 'report.json'
            with patch('tools.inventory_overlays.require_ignored', side_effect=InventoryError('blocked')):
                self.assertEqual(main(['--out', str(output)]), 1)
            self.assertFalse(output.exists())
