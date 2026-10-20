import json
import os
import shutil
import tempfile
import unittest
from pathlib import Path
import subprocess
import sys

REPO_ROOT = Path(__file__).resolve().parent.parent
VERIFIER = REPO_ROOT / "tools" / "verify_generation_manifest.py"
VALID_GUARDRAIL = {"max_entry_bytes": 131072, "max_return_gap_bytes": 65536, "violations": 0}

class TestGenerationManifestVerification(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.test_root = Path(self.temp_dir.name)

    def tearDown(self):
        self.temp_dir.cleanup()

    def test_missing_manifest(self):
        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_generation_manifest.py"),
            "--manifest",
            str(self.test_root / "nonexistent.json"),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("missing", res.stderr.lower() + res.stdout.lower())

    def test_wrong_elf_sha1(self):
        manifest_data = {
            "target_id": "SLES-50282",
            "elf_sha1": "0000000000000000000000000000000000000000",
            "function_map_source": "ghidra_export",
            "ps2recomp_commit": "eb8688b2fd191129f103a39543002c005b33409c",
            "ghidra_version": "11.1",
            "java_version": "25.0.4.1",
            "exporter_script": "ps2xRecomp/tools/ghidra/ExportPS2Functions.java",
            "exporter_script_sha256": "abcdef123456",
            "function_map_sha256": "123456abcdef",
            "toml_sha256": "7890abcdef12",
            "timestamp": "2026-10-19T12:00:00Z",
            "exported_function_count": 100,
            "generated_cpp_file_count": 10,
            "generated_output_dir": "generated/SLES-50282",
            "artifact_paths": [
                "analysis/generated/function_map.csv",
                "generated/SLES-50282"
            ]
        }
        manifest_file = self.test_root / "manifest.json"
        manifest_file.write_text(json.dumps(manifest_data), encoding="utf-8")

        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_generation_manifest.py"),
            "--manifest",
            str(manifest_file),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("checksum mismatch", res.stderr.lower() + res.stdout.lower())

    def test_fallback_analyzer_rejected_as_non_authoritative(self):
        manifest_data = {
            "target_id": "SLES-50282",
            "elf_sha1": "f478e19f2059d52899b691483ef5c7522e30e17c",
            "function_map_source": "fallback_analyzer",
            "ps2recomp_commit": "eb8688b2fd191129f103a39543002c005b33409c",
            "timestamp": "2026-10-19T12:00:00Z",
            "exported_function_count": 0,
            "generated_cpp_file_count": 6371,
            "artifact_paths": [
                "analysis/generated/function_map.csv",
                "generated/SLES-50282"
            ]
        }
        manifest_file = self.test_root / "manifest.json"
        manifest_file.write_text(json.dumps(manifest_data), encoding="utf-8")

        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_generation_manifest.py"),
            "--manifest",
            str(manifest_file),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("authoritative", res.stderr.lower() + res.stdout.lower())

    def test_ghidra_export_missing_ghidra_version_fails(self):
        manifest_data = {
            "target_id": "SLES-50282",
            "elf_sha1": "f478e19f2059d52899b691483ef5c7522e30e17c",
            "function_map_source": "ghidra_export",
            "ps2recomp_commit": "eb8688b2fd191129f103a39543002c005b33409c",
            "java_version": "25.0.4.1",
            "exporter_script": "ps2xRecomp/tools/ghidra/ExportPS2Functions.java",
            "exporter_script_sha256": "abcdef123456",
            "function_map_sha256": "123456abcdef",
            "toml_sha256": "7890abcdef12",
            "timestamp": "2026-10-19T12:00:00Z",
            "exported_function_count": 100,
            "generated_cpp_file_count": 10,
            "generated_output_dir": "generated/SLES-50282",
            "artifact_paths": [
                "analysis/generated/function_map.csv",
                "generated/SLES-50282"
            ]
        }
        manifest_file = self.test_root / "manifest.json"
        manifest_file.write_text(json.dumps(manifest_data), encoding="utf-8")

        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_generation_manifest.py"),
            "--manifest",
            str(manifest_file),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("ghidra_version", res.stderr.lower() + res.stdout.lower())

    def test_ghidra_export_missing_function_map_hash_fails(self):
        manifest_data = {
            "target_id": "SLES-50282",
            "elf_sha1": "f478e19f2059d52899b691483ef5c7522e30e17c",
            "function_map_source": "ghidra_export",
            "ps2recomp_commit": "eb8688b2fd191129f103a39543002c005b33409c",
            "ghidra_version": "11.1",
            "java_version": "25.0.4.1",
            "exporter_script": "ps2xRecomp/tools/ghidra/ExportPS2Functions.java",
            "exporter_script_sha256": "abcdef123456",
            "toml_sha256": "7890abcdef12",
            "timestamp": "2026-10-19T12:00:00Z",
            "exported_function_count": 100,
            "generated_cpp_file_count": 10,
            "generated_output_dir": "generated/SLES-50282",
            "artifact_paths": [
                "analysis/generated/function_map.csv",
                "generated/SLES-50282"
            ]
        }
        manifest_file = self.test_root / "manifest.json"
        manifest_file.write_text(json.dumps(manifest_data), encoding="utf-8")

        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_generation_manifest.py"),
            "--manifest",
            str(manifest_file),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("function_map_sha256", res.stderr.lower() + res.stdout.lower())

    def test_artifact_path_outside_ignored_directories(self):
        manifest_data = {
            "target_id": "SLES-50282",
            "elf_sha1": "f478e19f2059d52899b691483ef5c7522e30e17c",
            "function_map_source": "ghidra_export",
            "ps2recomp_commit": "eb8688b2fd191129f103a39543002c005b33409c",
            "ghidra_version": "11.1",
            "java_version": "25.0.4.1",
            "exporter_script": "ps2xRecomp/tools/ghidra/ExportPS2Functions.java",
            "exporter_script_sha256": "abcdef123456",
            "function_map_sha256": "123456abcdef",
            "toml_sha256": "7890abcdef12",
            "timestamp": "2026-10-19T12:00:00Z",
            "exported_function_count": 100,
            "generated_cpp_file_count": 10,
            "generated_output_dir": "generated/SLES-50282",
            "data_region_script_sha256": "fedcba654321",
            "function_map_guardrail": VALID_GUARDRAIL,
            "ghidra_language": "r5900:LE:32:default",
            "artifact_paths": [
                "src/tracked_code.cpp"
            ]
        }
        manifest_file = self.test_root / "manifest.json"
        manifest_file.write_text(json.dumps(manifest_data), encoding="utf-8")

        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_generation_manifest.py"),
            "--manifest",
            str(manifest_file),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("not ignored", res.stderr.lower() + res.stdout.lower())

    def test_valid_authoritative_manifest(self):
        manifest_data = {
            "target_id": "SLES-50282",
            "elf_sha1": "f478e19f2059d52899b691483ef5c7522e30e17c",
            "function_map_source": "ghidra_export",
            "ps2recomp_commit": "eb8688b2fd191129f103a39543002c005b33409c",
            "ghidra_version": "11.1",
            "java_version": "25.0.4.1",
            "exporter_script": "ps2xRecomp/tools/ghidra/ExportPS2Functions.java",
            "exporter_script_sha256": "abcdef123456",
            "function_map_sha256": "123456abcdef",
            "toml_sha256": "7890abcdef12",
            "timestamp": "2026-10-19T12:00:00Z",
            "exported_function_count": 100,
            "generated_cpp_file_count": 10,
            "generated_output_dir": "generated/SLES-50282",
            "data_region_script": "tools/ghidra/MarkDataRegions.java",
            "data_region_script_sha256": "fedcba654321",
            "function_map_guardrail": VALID_GUARDRAIL,
            "ghidra_language": "r5900:LE:32:default",
            "artifact_paths": [
                "analysis/generated/function_map.csv",
                "analysis/ghidra/SLES-50282.gpr",
                "generated/SLES-50282"
            ]
        }
        manifest_file = self.test_root / "manifest.json"
        manifest_file.write_text(json.dumps(manifest_data), encoding="utf-8")

        cmd = [
            sys.executable,
            str(REPO_ROOT / "tools" / "verify_generation_manifest.py"),
            "--manifest",
            str(manifest_file),
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, f"Failed: {res.stderr}\n{res.stdout}")
        self.assertIn("authoritative manifest verified successfully", res.stdout.lower())

class TestFunctionMapGuardrailInManifest(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.manifest_file = Path(self.temp_dir.name) / "manifest.json"

    def tearDown(self):
        self.temp_dir.cleanup()

    def manifest(self, **overrides):
        data = {
            "target_id": "SLES-50282",
            "elf_sha1": "f478e19f2059d52899b691483ef5c7522e30e17c",
            "function_map_source": "ghidra_export",
            "ps2recomp_commit": "eb8688b2fd191129f103a39543002c005b33409c",
            "ghidra_version": "11.1",
            "java_version": "25.0.4.1",
            "function_map_sha256": "123456abcdef",
            "toml_sha256": "7890abcdef12",
            "data_region_script": "tools/ghidra/MarkDataRegions.java",
            "data_region_script_sha256": "fedcba654321",
            "function_map_guardrail": dict(VALID_GUARDRAIL),
            "ghidra_language": "r5900:LE:32:default",
            "artifact_paths": ["analysis/generated/function_map.csv", "generated/SLES-50282"],
        }
        data.update(overrides)
        return {key: value for key, value in data.items() if value is not None}

    def verify(self, data):
        self.manifest_file.write_text(json.dumps(data), encoding="utf-8")
        command = [sys.executable, str(VERIFIER), "--manifest", str(self.manifest_file)]
        return subprocess.run(command, capture_output=True, text=True)

    def test_missing_guardrail_record_fails(self):
        result = self.verify(self.manifest(function_map_guardrail=None))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("function_map_guardrail", result.stderr)

    def test_guardrail_violations_fail(self):
        result = self.verify(self.manifest(function_map_guardrail=dict(VALID_GUARDRAIL, violations=3)))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("violation", result.stderr.lower())

    def test_missing_data_region_script_hash_fails(self):
        result = self.verify(self.manifest(data_region_script_sha256=None))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("data_region_script_sha256", result.stderr)

    def test_generic_mips_language_fails(self):
        result = self.verify(self.manifest(ghidra_language="MIPS:LE:64:64-32addr"))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ghidra_language", result.stderr)

    def test_complete_guardrail_record_passes(self):
        result = self.verify(self.manifest())
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
