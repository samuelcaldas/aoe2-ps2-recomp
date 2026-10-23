import struct
import unittest

from tools.map_overlays import OverlayMapError, match_overlays, parse_overlay_header

LOAD = 0x1D98000


def overlay(overlay_id, text, data, bss, load=LOAD, magic=b"MWo3"):
    header = magic + struct.pack("<7I", overlay_id, load, text, data, bss, load + text + data, load + text + data)
    return (header + b"x.bin").ljust(0x40, b"\0") + b"\0" * (text + data)


def reservation(index, memsz, address=LOAD):
    return {"segment_index": index, "virtual_address": address, "memory_bytes": memsz}


class TestOverlayHeader(unittest.TestCase):
    def test_parses_sizes_and_load_address(self):
        header = parse_overlay_header(overlay(4, 0x100, 0x40, 0x80))
        self.assertEqual((header.overlay_id, header.load_address, header.text, header.data, header.bss),
                         (4, LOAD, 0x100, 0x40, 0x80))

    def test_rejects_wrong_magic_and_short_files(self):
        with self.assertRaises(OverlayMapError):
            parse_overlay_header(overlay(4, 1, 1, 1, magic=b"XXXX"))
        with self.assertRaises(OverlayMapError):
            parse_overlay_header(b"MWo3")

    def test_rejects_file_shorter_than_declared_sections(self):
        with self.assertRaises(OverlayMapError):
            parse_overlay_header(overlay(4, 0x100, 0x40, 0)[:0x50])


class TestMatchOverlays(unittest.TestCase):
    def test_matches_by_id_and_checks_load_address_and_size(self):
        files = {"A.BIN": overlay(2, 0x100, 0, 0x40), "B.BIN": overlay(3, 0x20, 0, 0x1000)}
        matched = match_overlays(files, [reservation(2, 0x140), reservation(3, 0x1020)])
        self.assertEqual({m.file: m.segment_index for m in matched}, {"A.BIN": 2, "B.BIN": 3})

    def test_unmatched_reservation_fails(self):
        with self.assertRaises(OverlayMapError):
            match_overlays({"A.BIN": overlay(2, 1, 0, 0)}, [reservation(2, 0x40), reservation(3, 0x40)])

    def test_wrong_address_or_oversized_overlay_fails(self):
        with self.assertRaises(OverlayMapError):
            match_overlays({"A.BIN": overlay(2, 1, 0, 0)}, [reservation(2, 0x40, address=LOAD + 4)])
        with self.assertRaises(OverlayMapError):
            match_overlays({"A.BIN": overlay(2, 0x100, 0, 0)}, [reservation(2, 0x40)])

    def test_duplicate_overlay_id_fails(self):
        with self.assertRaises(OverlayMapError):
            match_overlays({"A.BIN": overlay(2, 1, 0, 0), "B.BIN": overlay(2, 1, 0, 0)}, [reservation(2, 0x40)])


if __name__ == "__main__":
    unittest.main()
