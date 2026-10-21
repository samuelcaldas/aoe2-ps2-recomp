import unittest

from tools.hd_assets.format_sniffer import sniff_format


class AssetFormatSnifferTest(unittest.TestCase):
    def test_identifies_jasc_palette_content_inside_bina_file(self):
        self.assertEqual(sniff_format(b"JASC-PAL\r\n0100\r\n256\r\n"), "jasc-pal")

    def test_identifies_windows_bitmap_content(self):
        self.assertEqual(sniff_format(b"BM" + b"\x00" * 30), "bmp")

    def test_preserves_unknown_for_unrecognized_data(self):
        self.assertEqual(sniff_format(b"unrecognized data"), "unknown")


if __name__ == "__main__":
    unittest.main()
