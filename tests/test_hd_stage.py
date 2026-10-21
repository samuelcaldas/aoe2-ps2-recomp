import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from tools.hd_assets.asset_catalog import AssetCatalog, AssetRecord
from tools.hd_assets.errors import ConflictError, HdAssetError
from tools.hd_assets.manifests import write_catalog
from tools.hd_assets.stage_plan import plan_stage, stage_root_for
from tools.hd_assets.staging import stage_assets
from tools.hd_assets.verifier import find_stage_problems


class StagePlanTest(unittest.TestCase):
    def test_empty_catalog_has_no_entries(self):
        catalog = AssetCatalog(())

        self.assertEqual(plan_stage(catalog, "desktop", Path("/repo")), ())

    def test_preserves_raw_interface_file_ids_and_extensions(self):
        catalog = AssetCatalog((record("interface", "resources/_common/drs/interface/50100.slp"),))

        entries = plan_stage(catalog, "switch", Path("/repo"))

        self.assertEqual(len(entries), 1)
        self.assertEqual(entries[0].destination_relative, "assets/interface/50100.slp")
        self.assertEqual(entries[0].source_relative, "resources/_common/drs/interface/50100.slp")

    def test_maps_datfile_into_provider_gamedata_category(self):
        catalog = AssetCatalog((record("datfile", "resources/_common/dat/empires2_x1_p1.dat"),))

        entries = plan_stage(catalog, "desktop", Path("/repo"))

        self.assertEqual(entries[0].destination_relative, "assets/gamedata/empires2_x1_p1.dat")

    def test_maps_many_categories_into_provider_layout(self):
        catalog = AssetCatalog((
            record("interface", "resources/_common/drs/interface/50100.slp"),
            record("graphics", "resources/_common/drs/graphics/100.slp"),
            record("terrain", "resources/_common/terrain/textures/1.dds"),
            record("gamedata", "resources/_common/drs/gamedata_x1/50001.bina"),
        ))

        entries = plan_stage(catalog, "android", Path("/repo"))

        self.assertEqual(
            tuple(entry.destination_relative for entry in entries),
            (
                "assets/gamedata/50001.bina",
                "assets/graphics/100.slp",
                "assets/interface/50100.slp",
                "assets/terrain/1.dds",
            ),
        )

    def test_rejects_unknown_platform(self):
        with self.assertRaises(HdAssetError):
            stage_root_for("dreamcast", Path("/repo"))

    def test_rejects_catalog_path_traversal(self):
        catalog = AssetCatalog((record("interface", "resources/_common/drs/interface/../../secret.slp"),))

        with self.assertRaises(HdAssetError):
            plan_stage(catalog, "desktop", Path("/repo"))

    def test_rejects_two_source_files_with_the_same_provider_path(self):
        catalog = AssetCatalog((
            record("interface", "resources/_common/drs/interface/one/50100.slp"),
            record("interface", "resources/_common/drs/interface/two/50100.slp"),
        ))

        with self.assertRaises(HdAssetError):
            plan_stage(catalog, "desktop", Path("/repo"))


class StageAssetsTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.source = self.root / "source"
        self.source.mkdir()
        self.repo = self.root / "repo"
        self.payload = b"2.0N raw SLP bytes"
        relative = "resources/_common/drs/interface/50100.slp"
        source_file = self.source / relative
        source_file.parent.mkdir(parents=True)
        source_file.write_bytes(self.payload)
        self.catalog = AssetCatalog((record("interface", relative, self.payload),))

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_stages_original_bytes_under_platform_assets_root(self):
        report = stage_assets(self.catalog, "switch", self.source, self.repo)
        staged = self.repo / report.relative_root / "assets" / "interface" / "50100.slp"

        self.assertEqual(staged.read_bytes(), self.payload)
        self.assertEqual(report.entry_count, 1)
        self.assertEqual(report, stage_assets(self.catalog, "switch", self.source, self.repo))
        self.assertEqual(find_stage_problems("switch", self.repo), [])

    def test_refuses_to_overwrite_modified_stage_file(self):
        report = stage_assets(self.catalog, "desktop", self.source, self.repo)
        staged = self.repo / report.relative_root / "assets" / "interface" / "50100.slp"
        staged.write_bytes(b"modified")

        with self.assertRaises(ConflictError):
            stage_assets(self.catalog, "desktop", self.source, self.repo)

    def test_refuses_catalog_source_symlink_outside_source_root(self):
        outside = self.root / "outside.slp"
        outside.write_bytes(self.payload)
        source_file = self.source / "resources/_common/drs/interface/50100.slp"
        source_file.unlink()
        os.symlink(outside, source_file)

        with self.assertRaises(HdAssetError):
            stage_assets(self.catalog, "desktop", self.source, self.repo)

    def test_refuses_destination_symlink_outside_stage_root(self):
        report = stage_assets(self.catalog, "switch", self.source, self.repo)
        interface = self.repo / report.relative_root / "assets" / "interface"
        (interface / "50100.slp").unlink()
        interface.rmdir()
        outside = self.root / "outside"
        outside.mkdir()
        interface.symlink_to(outside, target_is_directory=True)

        with self.assertRaises(HdAssetError):
            stage_assets(self.catalog, "switch", self.source, self.repo)
        self.assertEqual(list(outside.iterdir()), [])

    def test_refuses_partial_file_symlink_without_modifying_its_target(self):
        report = stage_assets(self.catalog, "switch", self.source, self.repo)
        staged = self.repo / report.relative_root / "assets" / "interface" / "50100.slp"
        staged.unlink()
        outside = self.root / "outside.slp"
        outside.write_bytes(b"preserve")
        staged.with_name(staged.name + ".partial").symlink_to(outside)

        with self.assertRaises(HdAssetError):
            stage_assets(self.catalog, "switch", self.source, self.repo)

        self.assertEqual(outside.read_bytes(), b"preserve")

    def test_verifier_detects_missing_stage_file(self):
        report = stage_assets(self.catalog, "android", self.source, self.repo)
        staged = self.repo / report.relative_root / "assets" / "interface" / "50100.slp"
        staged.unlink()

        self.assertTrue(any("missing copy" in problem for problem in find_stage_problems("android", self.repo)))

    def test_desktop_and_android_share_the_same_portable_asset_tree(self):
        stage_assets(self.catalog, "desktop", self.source, self.repo)
        stage_assets(self.catalog, "android", self.source, self.repo)

        self.assertEqual(find_stage_problems("desktop", self.repo), [])
        self.assertEqual(find_stage_problems("android", self.repo), [])

    def test_verifier_rejects_manifest_path_traversal(self):
        report = stage_assets(self.catalog, "switch", self.source, self.repo)
        manifest = self.repo / report.relative_root / "hd_stage.json"
        data = json.loads(manifest.read_text())
        data["entries"][0]["path"] = "assets/../../outside.slp"
        manifest.write_text(json.dumps(data))

        self.assertTrue(any("invalid path" in problem for problem in find_stage_problems("switch", self.repo)))

    def test_verifier_rejects_unlisted_files(self):
        report = stage_assets(self.catalog, "switch", self.source, self.repo)
        extra = self.repo / report.relative_root / "assets" / "interface" / "999.slp"
        extra.write_bytes(b"unlisted")

        self.assertTrue(any("unlisted files" in problem for problem in find_stage_problems("switch", self.repo)))

    def test_staging_rejects_assets_removed_from_a_later_catalog(self):
        report = stage_assets(self.catalog, "switch", self.source, self.repo)
        stale = self.repo / report.relative_root / "assets" / "interface" / "50100.slp"

        with self.assertRaises(HdAssetError):
            stage_assets(AssetCatalog(()), "switch", self.source, self.repo)

        self.assertEqual(stale.read_bytes(), self.payload)

    def test_verifier_rejects_file_symlink_outside_stage_root(self):
        report = stage_assets(self.catalog, "switch", self.source, self.repo)
        staged = self.repo / report.relative_root / "assets" / "interface" / "50100.slp"
        outside = self.root / "outside.slp"
        outside.write_bytes(self.payload)
        staged.unlink()
        staged.symlink_to(outside)

        problems = find_stage_problems("switch", self.repo)

        self.assertTrue(any("escapes stage root" in problem for problem in problems))


class StageCliTest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.root = Path(self.temp_dir.name)
        self.source = self.root / "source"
        relative = "resources/_common/drs/interface/50100.slp"
        source_file = self.source / relative
        source_file.parent.mkdir(parents=True)
        source_file.write_bytes(b"2.0N raw SLP bytes")
        self.catalog_path = self.root / "catalog.json"
        write_catalog(AssetCatalog((record("interface", relative, b"2.0N raw SLP bytes"),)), self.catalog_path)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_dry_run_prints_plan_without_writing_stage_files(self):
        result = self._run_stage("--dry-run")

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("assets/interface/50100.slp", result.stdout)
        self.assertFalse((self.root / "repo" / "build" / "hd-stage" / "switch").exists())

    def test_stage_command_copies_raw_file_and_manifest(self):
        result = self._run_stage()

        self.assertEqual(result.returncode, 0, result.stderr)
        staged = self.root / "repo" / "build" / "hd-stage" / "switch" / "assets" / "interface" / "50100.slp"
        self.assertEqual(staged.read_bytes(), b"2.0N raw SLP bytes")
        self.assertTrue((staged.parents[2] / "hd_stage.json").is_file())

    def _run_stage(self, *extra):
        tool = Path(__file__).resolve().parent.parent / "tools" / "stage_hd_assets.py"
        return subprocess.run([
            sys.executable, str(tool),
            "--platform", "switch",
            "--catalog", str(self.catalog_path),
            "--source-dir", str(self.source),
            "--repo-root", str(self.root / "repo"),
            *extra,
        ], capture_output=True, text=True)


def record(category, relative, data=b"asset"):
    import hashlib

    return AssetRecord(
        category=category,
        source_relative=relative,
        asset_id=50100 if relative.endswith("50100.slp") else None,
        asset_format="unknown",
        size=len(data),
        sha256=hashlib.sha256(data).hexdigest(),
    )


if __name__ == "__main__":
    unittest.main()
