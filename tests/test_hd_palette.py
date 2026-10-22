import os
import unittest
from pathlib import Path

from tools.hd_assets.errors import HdAssetError
from tools.hd_assets.palette import read_jasc_palette


class JascPaletteTest(unittest.TestCase):
    def test_reads_256_rgb_colors_from_jasc_palette(self):
        data = b"JASC-PAL\r\n0100\r\n256\r\n0 0 0\r\n128 0 0\r\n" + b"1 2 3\r\n" * 254

        palette = read_jasc_palette(data)

        self.assertEqual(len(palette), 256)
        self.assertEqual(palette[0], (0, 0, 0, 255))
        self.assertEqual(palette[1], (128, 0, 0, 255))
        self.assertEqual(palette[-1], (1, 2, 3, 255))

    def test_rejects_non_jasc_header(self):
        with self.assertRaises(HdAssetError):
            read_jasc_palette(b"not a palette")

    def test_rejects_wrong_palette_size(self):
        data = b"JASC-PAL\n0100\n2\n0 0 0\n128 0 0\n"

        with self.assertRaises(HdAssetError):
            read_jasc_palette(data)

    def test_rejects_truncated_color_table(self):
        data = b"JASC-PAL\n0100\n256\n0 0 0\n"

        with self.assertRaises(HdAssetError):
            read_jasc_palette(data)

    def test_rejects_colors_outside_byte_range(self):
        data = b"JASC-PAL\n0100\n1\n256 0 0\n"

        with self.assertRaises(HdAssetError):
            read_jasc_palette(data)

    def test_rejects_non_rgb_color_lines(self):
        data = b"JASC-PAL\n0100\n1\n0 0\n"

        with self.assertRaises(HdAssetError):
            read_jasc_palette(data)


@unittest.skipUnless(
    os.environ.get("AOE2_HD_INTERFACE_SOURCE"),
    "set AOE2_HD_INTERFACE_SOURCE to the imported HD interface folder",
)
class RealPaletteFilesTest(unittest.TestCase):
    def test_all_jasc_pal_files_in_the_interface_catalog_parse(self):
        root = Path(os.environ["AOE2_HD_INTERFACE_SOURCE"])
        palette_files = [path for path in root.iterdir() if path.is_file() and path.read_bytes().startswith(b"JASC-PAL")]

        self.assertEqual(len(palette_files), 32)
        for path in palette_files:
            with self.subTest(palette=path.name):
                self.assertEqual(len(read_jasc_palette(path.read_bytes())), 256)


if __name__ == "__main__":
    unittest.main()
