#!/usr/bin/env python3
"""Execute authoritative Ghidra export and PS2Recomp code generation pipeline.

Workflow:
1. Verify target game ELF and SHA-1.
2. Verify PS2Recomp build and commit against lockfile.
3. Validate Ghidra installation (GHIDRA_HOME and support/analyzeHeadless).
   STOPS FAST IF GHIDRA IS MISSING. Does not substitute fallback analyzer.
4. Execute headless Ghidra analysis & export using ExportPS2Functions.java.
5. Create minimal clean PS2Recomp TOML configuration without stubs/skips/patches.
6. Run PS2Recomp generation into generated/SLES-50282/.
7. Write and verify authoritative generation_manifest.json.
"""
import argparse
import datetime
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))

from core.target import SINGLE_TARGET

def compute_sha1(filepath: Path) -> str:
    h = hashlib.sha1()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().lower()

def compute_sha256(filepath: Path) -> str:
    h = hashlib.sha256()
    with open(filepath, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest().lower()

def count_csv_records(csv_path: Path) -> int:
    if not csv_path.exists():
        return 0
    lines = [line.strip() for line in csv_path.read_text(encoding="utf-8", errors="replace").splitlines() if line.strip()]
    return max(0, len(lines) - 1)

def count_cpp_files(output_dir: Path) -> int:
    if not output_dir.exists():
        return 0
    return len(list(output_dir.glob("*.cpp")))

def get_java_version() -> str:
    try:
        res = subprocess.run(["java", "-version"], capture_output=True, text=True, check=True)
        # java -version outputs to stderr
        output = res.stderr or res.stdout
        first_line = output.splitlines()[0] if output else "unknown"
        return first_line.strip()
    except Exception as e:
        return f"unknown ({e})"

def main():
    parser = argparse.ArgumentParser(description="Authoritative Ghidra export and PS2Recomp code generation.")
    parser.parse_args()

    # 1. Verify ELF
    elf_path = (REPO_ROOT / SINGLE_TARGET.relative_dest_path).resolve()
    if not elf_path.exists() or not elf_path.is_file():
        sys.stderr.write(f"Error: Target ELF not found at {elf_path}\n")
        sys.exit(1)

    elf_sha1 = compute_sha1(elf_path)
    if elf_sha1 != SINGLE_TARGET.expected_sha1:
        sys.stderr.write(f"Error: Checksum mismatch for {elf_path}: {elf_sha1}\n")
        sys.exit(1)

    # 2. Check PS2Recomp commit against lockfile
    lock_file = REPO_ROOT / "third_party_provenance.lock.json"
    if not lock_file.exists():
        sys.stderr.write(f"Error: Lockfile missing: {lock_file}\n")
        sys.exit(1)

    lock_data = json.loads(lock_file.read_text(encoding="utf-8"))
    expected_ps2recomp_sha = lock_data["dependencies"]["PS2Recomp"]["commit_sha"]

    ps2recomp_dir = REPO_ROOT / "third_party" / "PS2Recomp"
    actual_ps2recomp_sha = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ps2recomp_dir, text=True
    ).strip()

    if actual_ps2recomp_sha != expected_ps2recomp_sha:
        sys.stderr.write(
            f"Error: PS2Recomp commit mismatch!\n"
            f"Expected: {expected_ps2recomp_sha}\n"
            f"Actual:   {actual_ps2recomp_sha}\n"
        )
        sys.exit(1)

    # Ensure PS2Recomp binary is built
    ps2_recomp_bin = ps2recomp_dir / "build" / "ps2xRecomp" / "ps2_recomp"
    if not ps2_recomp_bin.exists():
        sys.stderr.write(f"Error: ps2_recomp binary not built at {ps2_recomp_bin}\n")
        sys.exit(1)

    # 3. Check Ghidra environment (strict fail-fast)
    ghidra_home = os.environ.get("GHIDRA_HOME")
    if not ghidra_home:
        sys.stderr.write(
            "CRITICAL BLOCKER: GHIDRA_HOME environment variable is not set.\n"
            "Ghidra is an external prerequisite required to generate the authoritative function map.\n"
            "Fallback analysis is strictly prohibited for authoritative code generation.\n"
            "Please install Ghidra (10.x or later) and export GHIDRA_HOME=/path/to/ghidra.\n"
        )
        sys.exit(1)

    ghidra_path = Path(ghidra_home).resolve()
    analyze_headless = ghidra_path / "support" / "analyzeHeadless"
    if not analyze_headless.exists() and not (ghidra_path / "support" / "analyzeHeadless.bat").exists():
        sys.stderr.write(f"CRITICAL BLOCKER: analyzeHeadless not found under {ghidra_path / 'support'}\n")
        sys.exit(1)

    exporter_script = ps2recomp_dir / "ps2xRecomp" / "tools" / "ghidra" / "ExportPS2Functions.java"
    if not exporter_script.exists():
        sys.stderr.write(f"Error: Exporter script missing: {exporter_script}\n")
        sys.exit(1)

    analysis_dir = REPO_ROOT / "analysis"
    ghidra_proj_dir = analysis_dir / "ghidra"
    generated_analysis_dir = analysis_dir / "generated"
    output_cpp_dir = REPO_ROOT / "generated" / SINGLE_TARGET.serial

    ghidra_proj_dir.mkdir(parents=True, exist_ok=True)
    generated_analysis_dir.mkdir(parents=True, exist_ok=True)
    output_cpp_dir.mkdir(parents=True, exist_ok=True)

    csv_map = generated_analysis_dir / "function_map.csv"
    toml_config = generated_analysis_dir / "SLES-50282.toml"

    # 4. Execute Ghidra headless analysis & export
    print(f"Executing authoritative headless Ghidra export using {exporter_script}...")
    cmd = [
        str(analyze_headless),
        str(ghidra_proj_dir),
        "SLES_50282_Project",
        "-import",
        str(elf_path),
        "-overwrite",
        "-processor",
        "MIPS:LE:64:64-32addr",
        "-scriptPath",
        str(exporter_script.parent),
        "-postScript",
        "ExportPS2Functions.java",
        str(toml_config),
        str(csv_map),
    ]
    subprocess.run(cmd, check=True)

    if not csv_map.exists() or not toml_config.exists():
        sys.stderr.write("Error: Ghidra export script did not produce expected output files!\n")
        sys.exit(1)

    # 5. Verify TOML configuration output path
    toml_content = toml_config.read_text(encoding="utf-8")
    lines = toml_content.splitlines()
    new_lines = []
    for line in lines:
        if line.strip().startswith("output ="):
            new_lines.append(f'output = "{output_cpp_dir}"')
        elif line.strip().startswith("ghidra_output ="):
            new_lines.append(f'ghidra_output = "{csv_map}"')
        elif line.strip().startswith("single_file_output ="):
            new_lines.append("single_file_output = false")
        else:
            new_lines.append(line)
    toml_config.write_text("\n".join(new_lines) + "\n", encoding="utf-8")

    # 6. Execute PS2Recomp code generation
    print("Executing PS2Recomp code generation from authoritative Ghidra map...")
    subprocess.run([str(ps2_recomp_bin), str(toml_config)], check=True)

    # 7. Write authoritative generation manifest
    exported_count = count_csv_records(csv_map)
    cpp_count = count_cpp_files(output_cpp_dir)

    manifest_data = {
        "target_id": SINGLE_TARGET.serial,
        "elf_sha1": elf_sha1,
        "function_map_source": "ghidra_export",
        "ps2recomp_commit": actual_ps2recomp_sha,
        "ghidra_version": ghidra_path.name,
        "java_version": get_java_version(),
        "exporter_script": str(exporter_script.relative_to(REPO_ROOT)),
        "exporter_script_sha256": compute_sha256(exporter_script),
        "function_map_sha256": compute_sha256(csv_map),
        "toml_sha256": compute_sha256(toml_config),
        "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "exported_function_count": exported_count,
        "generated_cpp_file_count": cpp_count,
        "generated_output_dir": str(output_cpp_dir.relative_to(REPO_ROOT)),
        "artifact_paths": [
            str(csv_map.relative_to(REPO_ROOT)),
            str(toml_config.relative_to(REPO_ROOT)),
            str(ghidra_proj_dir.relative_to(REPO_ROOT)),
            str(output_cpp_dir.relative_to(REPO_ROOT)),
        ],
    }

    manifest_path = REPO_ROOT / "generation_manifest.json"
    manifest_path.write_text(json.dumps(manifest_data, indent=2) + "\n", encoding="utf-8")
    print(f"Authoritative generation manifest written to {manifest_path}")

    # Verify manifest immediately
    verify_cmd = [sys.executable, str(REPO_ROOT / "tools" / "verify_generation_manifest.py")]
    subprocess.run(verify_cmd, check=True)

if __name__ == "__main__":
    main()
