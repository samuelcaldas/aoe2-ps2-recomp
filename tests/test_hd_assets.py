import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.hd_source import HD_2013
from tools.hd_assets.asset_catalog import AssetCatalog
from tools.hd_assets.edition_detector import validate_source
from tools.hd_assets.errors import (
    ConflictError,
    EditionMismatchError,
    HdAssetError,
    IntegrityError,
    MissingCategoryError,
)
from tools.hd_assets.format_sniffer import sniff_format
from tools.hd_assets.importer import import_assets
from tools.hd_assets.inventory import render_inventory
from tools.hd_assets.manifests import (
    build_tracked_summary,
    read_catalog,
    read_tracked,
    write_catalog,
    write_tracked,
)
from tools.hd_assets.slp_header import read_slp_size
from tools.hd_assets.verifier import find_copy_problems, find_summary_problems

REAL_SOURCE = os.environ.get("AOE2_HD_SOURCE")
INTERFACE_RELATIVE = HD_2013.path_for("interface")


def write_file(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def make_install(root: Path) -> Path:
    for sentinel in HD_2013.sentinel_paths:
        write_file(root / sentinel, b"sentinel")
    for category in HD_2013.category_names():
        relative = HD_2013.path_for(category)
        if not Path(relative).suffix:
            (root / relative).mkdir(parents=True, exist_ok=True)
    return root


def run_tool(name: str, *args: str) -> subprocess.CompletedProcess:
    cmd = [sys.executable, str(REPO_ROOT / "tools" / f"{name}.py"), *args]
    return subprocess.run(cmd, capture_output=True, text=True)


def slp_bytes(width: int, height: int, frames: int = 1) -> bytes:
    header = struct.pack("<4si24s", b"2.0N", frames, b"comment".ljust(24, b"\x00"))
    frame = struct.pack("<4I4i", 0, 0, 0, 0, width, height, 0, 0)
    return header + frame + b"\x00" * 16


class FormatSnifferTest(unittest.TestCase):
    def test_identifies_slp_versions(self):
        self.assertEqual(sniff_format(b"2.0N" + b"\x01" * 28), "slp-2.0N")
        self.assertEqual(sniff_format(b"4.2P" + b"\x01" * 28), "slp-4.2P")

    def test_identifies_signed_containers(self):
        self.assertEqual(sniff_format(b"SLDX" + b"\x00" * 28), "sld")
        self.assertEqual(sniff_format(b"SMPX" + b"\x00" * 28), "smpx")

    def test_unknown_and_empty_headers(self):
        self.assertEqual(sniff_format(b"\x00" * 32), "unknown")
        self.assertEqual(sniff_format(b""), "unknown")


class TempInstallTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()


class EditionDetectorTest(TempInstallTest):
    def test_rejects_missing_folder(self):
        with self.assertRaises(EditionMismatchError):
            validate_source(self.root / "absent", HD_2013, ("interface",))

    def test_rejects_missing_sentinel(self):
        make_install(self.root)
        (self.root / "AoK HD.exe").unlink()
        with self.assertRaises(EditionMismatchError):
            validate_source(self.root, HD_2013, ("interface",))

    def test_rejects_missing_category(self):
        make_install(self.root)
        shutil.rmtree(self.root / INTERFACE_RELATIVE)
        with self.assertRaises(MissingCategoryError):
            validate_source(self.root, HD_2013, ("interface",))

    def test_rejects_unknown_category(self):
        make_install(self.root)
        with self.assertRaises(HdAssetError):
            validate_source(self.root, HD_2013, ("sounds",))

    def test_rejects_empty_category_request(self):
        make_install(self.root)
        with self.assertRaises(HdAssetError):
            validate_source(self.root, HD_2013, ())

    def test_accepts_valid_install(self):
        make_install(self.root)
        validate_source(self.root, HD_2013, ("interface",))


class AssetCatalogTest(TempInstallTest):
    def test_empty_category_has_no_records(self):
        make_install(self.root)
        catalog = AssetCatalog.scan(self.root, HD_2013, ("interface",))
        self.assertEqual(catalog.total(), 0)

    def test_single_numeric_asset(self):
        make_install(self.root)
        write_file(self.root / INTERFACE_RELATIVE / "1234.slp", slp_bytes(640, 480))
        catalog = AssetCatalog.scan(self.root, HD_2013, ("interface",))
        record = catalog.records[0]
        self.assertEqual(record.asset_id, 1234)
        self.assertEqual(record.asset_format, "slp-2.0N")
        self.assertEqual(record.category, "interface")
        self.assertEqual(record.source_relative, f"{INTERFACE_RELATIVE}/1234.slp")

    def test_many_assets_including_non_numeric_and_nested(self):
        make_install(self.root)
        interface = self.root / INTERFACE_RELATIVE
        write_file(interface / "1.slp", slp_bytes(640, 480))
        write_file(interface / "nested" / "2.slp", slp_bytes(800, 600))
        write_file(interface / "notes.txt", b"plain text")
        catalog = AssetCatalog.scan(self.root, HD_2013, ("interface",))
        self.assertEqual(catalog.total(), 3)
        self.assertEqual(catalog.counts_by_format(), {"slp-2.0N": 2, "unknown": 1})
        self.assertEqual(sorted(r.asset_id for r in catalog.records if r.asset_id is not None), [1, 2])

    def test_symlinks_are_skipped(self):
        make_install(self.root)
        os.symlink(self.root / "AoK HD.exe", self.root / INTERFACE_RELATIVE / "9.slp")
        catalog = AssetCatalog.scan(self.root, HD_2013, ("interface",))
        self.assertEqual(catalog.total(), 0)

    def test_single_file_category(self):
        make_install(self.root)
        catalog = AssetCatalog.scan(self.root, HD_2013, ("datfile",))
        self.assertEqual(catalog.total(), 1)
        self.assertIsNone(catalog.records[0].asset_id)


class ImporterTest(TempInstallTest):
    def setUp(self):
        super().setUp()
        self.source = make_install(self.root / "install")
        self.dest = self.root / "game"
        self.payload = slp_bytes(640, 480)
        write_file(self.source / INTERFACE_RELATIVE / "1.slp", self.payload)

    def test_copies_only_selected_category_and_verifies(self):
        catalog = import_assets(self.source, HD_2013, ("interface",), self.dest)
        copied = self.dest / "source" / INTERFACE_RELATIVE / "1.slp"
        self.assertEqual(copied.read_bytes(), self.payload)
        self.assertEqual(catalog.total(), 1)
        self.assertFalse((self.dest / "source" / HD_2013.path_for("graphics")).exists())

    def test_reimport_is_a_noop(self):
        first = import_assets(self.source, HD_2013, ("interface",), self.dest)
        second = import_assets(self.source, HD_2013, ("interface",), self.dest)
        self.assertEqual(first.to_rows(), second.to_rows())

    def test_refuses_conflicting_destination(self):
        import_assets(self.source, HD_2013, ("interface",), self.dest)
        copied = self.dest / "source" / INTERFACE_RELATIVE / "1.slp"
        copied.write_bytes(b"changed")
        with self.assertRaises(ConflictError):
            import_assets(self.source, HD_2013, ("interface",), self.dest)
        self.assertEqual(copied.read_bytes(), b"changed")

    def test_corrupted_copy_is_rejected_and_leaves_no_partial(self):
        def write_wrong_bytes(_source, partial):
            Path(partial).write_bytes(b"corrupt")

        with mock.patch("tools.hd_assets.importer.shutil.copyfile", side_effect=write_wrong_bytes):
            with self.assertRaises(IntegrityError):
                import_assets(self.source, HD_2013, ("interface",), self.dest)
        target_dir = self.dest / "source" / INTERFACE_RELATIVE
        self.assertEqual(list(target_dir.glob("*")), [])


class VerifierTest(TempInstallTest):
    def setUp(self):
        super().setUp()
        self.source = make_install(self.root / "install")
        self.dest = self.root / "game"
        write_file(self.source / INTERFACE_RELATIVE / "1.slp", slp_bytes(640, 480))
        self.catalog = import_assets(self.source, HD_2013, ("interface",), self.dest)

    def test_clean_import_has_no_problems(self):
        tracked = build_tracked_summary(HD_2013, self.catalog)
        self.assertEqual(find_copy_problems(self.dest, self.catalog), [])
        self.assertEqual(find_summary_problems(HD_2013, self.catalog, tracked), [])

    def test_tracked_summary_contains_no_file_names(self):
        text = json.dumps(build_tracked_summary(HD_2013, self.catalog))
        self.assertNotIn("resources", text)
        self.assertNotIn("1.slp", text)

    def test_flipped_byte_is_reported(self):
        copied = self.dest / "source" / INTERFACE_RELATIVE / "1.slp"
        data = bytearray(copied.read_bytes())
        data[-1] ^= 0xFF
        copied.write_bytes(bytes(data))
        problems = find_copy_problems(self.dest, self.catalog)
        self.assertTrue(any("digest mismatch" in problem for problem in problems))

    def test_missing_copy_is_reported(self):
        (self.dest / "source" / INTERFACE_RELATIVE / "1.slp").unlink()
        problems = find_copy_problems(self.dest, self.catalog)
        self.assertTrue(any("missing copy" in problem for problem in problems))

    def test_tracked_summary_rejects_path_like_and_extra_keys(self):
        tracked = build_tracked_summary(HD_2013, self.catalog)
        tracked["counts"]["by_format"]["a/b"] = 1
        tracked["extra"] = 1
        problems = find_summary_problems(HD_2013, self.catalog, tracked)
        self.assertTrue(any("path-like" in problem for problem in problems))
        self.assertTrue(any("unexpected keys" in problem for problem in problems))


class CatalogRoundTripTest(TempInstallTest):
    def test_catalog_and_tracked_summary_round_trip(self):
        source = make_install(self.root / "install")
        write_file(source / INTERFACE_RELATIVE / "7.slp", slp_bytes(800, 600))
        catalog = AssetCatalog.scan(source, HD_2013, ("interface",))
        catalog_path = self.root / "catalog.json"
        tracked_path = self.root / "tracked.json"
        write_catalog(catalog, catalog_path)
        write_tracked(build_tracked_summary(HD_2013, catalog), tracked_path)
        self.assertEqual(read_catalog(catalog_path).to_rows(), catalog.to_rows())
        self.assertEqual(read_tracked(tracked_path), build_tracked_summary(HD_2013, catalog))

    def test_merge_keeps_one_record_per_file(self):
        source = make_install(self.root / "install")
        write_file(source / INTERFACE_RELATIVE / "7.slp", slp_bytes(800, 600))
        catalog = AssetCatalog.scan(source, HD_2013, ("interface",))
        self.assertEqual(catalog.merged_with(catalog).total(), 1)


class SlpHeaderTest(TempInstallTest):
    def test_reads_frame_count_and_first_frame_size(self):
        path = self.root / "3.slp"
        write_file(path, slp_bytes(640, 480, frames=3))
        size = read_slp_size(path)
        self.assertEqual((size.frame_count, size.width, size.height), (3, 640, 480))

    def test_rejects_truncated_header(self):
        path = self.root / "bad.slp"
        write_file(path, b"2.0N\x00\x00")
        with self.assertRaises(HdAssetError):
            read_slp_size(path)


class InventoryTest(TempInstallTest):
    def test_reports_sizes_per_category(self):
        source = make_install(self.root / "install")
        interface = source / INTERFACE_RELATIVE
        write_file(interface / "1.slp", slp_bytes(640, 480))
        write_file(interface / "2.slp", slp_bytes(640, 480))
        write_file(interface / "3.slp", slp_bytes(800, 600))
        dest = self.root / "game"
        catalog = import_assets(source, HD_2013, ("interface",), dest)
        text = render_inventory(catalog, dest)
        self.assertIn("640x480 x2", text)
        self.assertIn("800x600 x1", text)


class ImportCliTest(TempInstallTest):
    def test_rejects_missing_source(self):
        result = run_tool(
            "import_hd_assets",
            "--source", str(self.root / "absent"),
            "--dest-dir", str(self.root / "game"),
            "--tracked-manifest", str(self.root / "tracked.json"),
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not a directory", result.stderr)


@unittest.skipUnless(REAL_SOURCE, "set AOE2_HD_SOURCE to a local HD install to run")
class RealInstallTest(TempInstallTest):
    def test_interface_imports_and_verifies(self):
        dest = self.root / "game"
        tracked = self.root / "tracked.json"
        imported = run_tool(
            "import_hd_assets",
            "--source", REAL_SOURCE,
            "--categories", "interface",
            "--dest-dir", str(dest),
            "--tracked-manifest", str(tracked),
        )
        self.assertEqual(imported.returncode, 0, imported.stderr)
        verified = run_tool(
            "verify_hd_assets",
            "--dest-dir", str(dest),
            "--tracked-manifest", str(tracked),
        )
        self.assertEqual(verified.returncode, 0, verified.stderr)


if __name__ == "__main__":
    unittest.main()
