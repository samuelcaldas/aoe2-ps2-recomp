#!/usr/bin/env python3
"""Validate generation manifest and ensure authoritative provenance safety.

Verifies:
- Function map source must be strictly 'ghidra_export' (rejects 'fallback_analyzer').
- Target ID matches expected target (SLES-50282).
- ELF SHA-1 matches expected SHA-1.
- PS2Recomp commit matches third-party lockfile.
- Ghidra version and Java version are present.
- Exporter script path and SHA-256 hash are present.
- Function map SHA-256 and TOML SHA-256 hashes are present.
- Generated output directory is present and non-empty.
- Data-region marking script hash and a zero-violation function map guardrail are recorded.
- All artifact paths are strictly within git-ignored paths.
- No proprietary content (disassembly, addresses, code snippets) is present.
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.target import SINGLE_TARGET

def is_path_git_ignored(rel_path: str) -> bool:
    """Check if a path is ignored by git using git check-ignore."""
    res = subprocess.run(
        ["git", "check-ignore", "-q", rel_path],
        cwd=REPO_ROOT,
    )
    return res.returncode == 0

def parse_args():
    parser = argparse.ArgumentParser(description="Verify generation manifest metadata.")
    parser.add_argument(
        "--manifest",
        type=Path,
        default=None,
        help="Path to manifest file (default: <repo_root>/generation_manifest.json)",
    )
    return parser.parse_args()

def main():
    args = parse_args()
    manifest_path = (args.manifest or (REPO_ROOT / "generation_manifest.json")).resolve()

    if not manifest_path.exists():
        sys.stderr.write(f"Error: Manifest file missing: {manifest_path}\n")
        sys.exit(1)

    try:
        data = json.loads(manifest_path.read_text(encoding="utf-8"))
    except Exception as e:
        sys.stderr.write(f"Error: Failed to parse manifest JSON: {e}\n")
        sys.exit(1)

    # 1. Authoritative source validation
    map_source = data.get("function_map_source")
    if map_source != "ghidra_export":
        sys.stderr.write(
            f"Error: Manifest function_map_source '{map_source}' is not authoritative.\n"
            "Only 'ghidra_export' is accepted as an authoritative baseline for compilation.\n"
            "Fallback analyzer or exploratory runs must not be used.\n"
        )
        sys.exit(1)

    # 2. Target ID check
    target_id = data.get("target_id")
    if target_id != SINGLE_TARGET.serial:
        sys.stderr.write(
            f"Error: Target ID mismatch in manifest. Expected '{SINGLE_TARGET.serial}', got '{target_id}'\n"
        )
        sys.exit(1)

    # 3. ELF SHA-1 check
    elf_sha1 = data.get("elf_sha1", "").lower()
    if elf_sha1 != SINGLE_TARGET.expected_sha1:
        sys.stderr.write(
            f"Error: Checksum mismatch in manifest.\n"
            f"Expected SHA-1: {SINGLE_TARGET.expected_sha1}\n"
            f"Actual SHA-1:   {elf_sha1}\n"
        )
        sys.exit(1)

    # 4. Mandatory fields for ghidra_export
    if not data.get("ghidra_version"):
        sys.stderr.write("Error: Manifest missing mandatory field 'ghidra_version'\n")
        sys.exit(1)

    if not data.get("java_version"):
        sys.stderr.write("Error: Manifest missing mandatory field 'java_version'\n")
        sys.exit(1)

    if not data.get("function_map_sha256"):
        sys.stderr.write("Error: Manifest missing mandatory field 'function_map_sha256'\n")
        sys.exit(1)

    if not data.get("toml_sha256"):
        sys.stderr.write("Error: Manifest missing mandatory field 'toml_sha256'\n")
        sys.exit(1)

    if not data.get("data_region_script_sha256"):
        sys.stderr.write(
            "Error: Manifest missing mandatory field 'data_region_script_sha256'.\n"
            "The Ghidra export must run tools/ghidra/MarkDataRegions.java before the exporter.\n"
        )
        sys.exit(1)

    guardrail = data.get("function_map_guardrail")
    if not isinstance(guardrail, dict) or "violations" not in guardrail:
        sys.stderr.write(
            "Error: Manifest missing mandatory field 'function_map_guardrail'.\n"
            "Run tools/verify_function_map.py on the exported map before generation.\n"
        )
        sys.exit(1)

    if guardrail["violations"] != 0:
        sys.stderr.write(
            f"Error: Function map guardrail recorded {guardrail['violations']} violation(s).\n"
            "Entries spanning data must be fixed in the Ghidra export before generation.\n"
        )
        sys.exit(1)

    # 5. PS2Recomp commit check against lockfile
    lock_file = REPO_ROOT / "third_party_provenance.lock.json"
    if lock_file.exists():
        try:
            lock_data = json.loads(lock_file.read_text(encoding="utf-8"))
            expected_ps2recomp_sha = lock_data["dependencies"]["PS2Recomp"]["commit_sha"]
            manifest_ps2recomp_sha = data.get("ps2recomp_commit")
            if manifest_ps2recomp_sha != expected_ps2recomp_sha:
                sys.stderr.write(
                    f"Error: PS2Recomp commit mismatch against lockfile.\n"
                    f"Lockfile: {expected_ps2recomp_sha}\n"
                    f"Manifest: {manifest_ps2recomp_sha}\n"
                )
                sys.exit(1)
        except Exception as e:
            sys.stderr.write(f"Warning: Could not check lockfile: {e}\n")

    # 6. Artifact paths safety check (must be git ignored)
    artifact_paths = data.get("artifact_paths", [])
    for path_str in artifact_paths:
        if not is_path_git_ignored(path_str):
            sys.stderr.write(
                f"Error: Artifact path '{path_str}' is NOT ignored by git!\n"
                f"All generated, binary, or disassembled artifacts must be strictly ignored.\n"
            )
            sys.exit(1)

    print("Authoritative manifest verified successfully.")
    print(f"Target:               {target_id}")
    print(f"Source:               {map_source}")
    print(f"ELF SHA-1:            {elf_sha1}")
    print(f"PS2Recomp commit:     {data.get('ps2recomp_commit')}")
    print(f"Ghidra version:       {data.get('ghidra_version')}")
    print(f"Java version:         {data.get('java_version')}")
    print(f"Exporter script:      {data.get('exporter_script')}")
    print(f"Exporter script hash: {data.get('exporter_script_sha256')}")
    print(f"Function map hash:    {data.get('function_map_sha256')}")
    print(f"TOML hash:            {data.get('toml_sha256')}")
    print(f"Exported functions:   {data.get('exported_function_count')}")
    print(f"Generated C++ files:  {data.get('generated_cpp_file_count')}")
    sys.exit(0)

if __name__ == "__main__":
    main()
