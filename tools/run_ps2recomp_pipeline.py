#!/usr/bin/env python3
"""Execute authoritative Ghidra export and PS2Recomp code generation pipeline.

Workflow:
1. Verify target game ELF and SHA-1.
2. Verify PS2Recomp build and commit against lockfile.
3. Validate Ghidra installation (GHIDRA_HOME and support/analyzeHeadless).
   STOPS FAST IF GHIDRA IS MISSING. Does not substitute fallback analyzer.
4. Execute headless Ghidra analysis, mark return-free data regions as
   non-executable (tools/ghidra/MarkDataRegions.java), then export using
   ExportPS2Functions.java.
5. Reject the export if tools/verify_function_map.py finds entries spanning data.
6. Create minimal clean PS2Recomp TOML configuration without stubs/skips/patches.
7. Regenerate generated/SLES-50282/ from scratch with PS2Recomp.
8. Write and verify authoritative generation_manifest.json.
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
from tools import install_ghidra_ee_extension as ee_extension
from tools import verify_function_map as function_map_guardrail

DATA_REGION_SCRIPT = REPO_ROOT / "tools" / "ghidra" / "MarkDataRegions.java"

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

def build_ghidra_command(analyze_headless: Path, project_dir: Path, elf_path: Path,
                         exporter_script: Path, toml_config: Path, csv_map: Path) -> list:
    """Compose the headless Ghidra run: import + analysis, data-region marking, then export."""
    script_path = f"{DATA_REGION_SCRIPT.parent};{exporter_script.parent}"
    max_return_gap = str(function_map_guardrail.DEFAULT_MAX_RETURN_GAP_BYTES)
    return [
        str(analyze_headless), str(project_dir), "SLES_50282_Project",
        "-import", str(elf_path), "-overwrite",
        "-processor", ee_extension.LANGUAGE_ID,
        "-scriptPath", script_path,
        "-postScript", DATA_REGION_SCRIPT.name, max_return_gap,
        "-postScript", exporter_script.name, str(toml_config), str(csv_map),
    ]

def enforce_function_map_guardrail(csv_map: Path, elf_path: Path) -> dict:
    """Fail the pipeline before code generation if any entry spans data; return the guardrail record."""
    policy = function_map_guardrail.GuardrailPolicy(
        function_map_guardrail.DEFAULT_MAX_ENTRY_BYTES,
        function_map_guardrail.DEFAULT_MAX_RETURN_GAP_BYTES,
    )
    entries = function_map_guardrail.load_function_map(csv_map)
    image = function_map_guardrail.LoadImage.from_elf(elf_path)
    violations = function_map_guardrail.find_violations(entries, image, policy)
    if function_map_guardrail.report(entries, violations) != function_map_guardrail.EXIT_OK:
        sys.stderr.write("Error: function map guardrail failed; refusing to generate code from data.\n")
        sys.exit(1)
    return {
        "max_entry_bytes": policy.max_entry_bytes,
        "max_return_gap_bytes": policy.max_return_gap_bytes,
        "violations": 0,
    }

def reset_generated_output(output_cpp_dir: Path) -> None:
    """Remove stale generated sources so the output reflects only the current map."""
    expected_dir = (REPO_ROOT / "generated" / SINGLE_TARGET.serial).resolve()
    if output_cpp_dir.resolve() != expected_dir:
        sys.stderr.write(f"Error: refusing to clear unexpected directory {output_cpp_dir}\n")
        sys.exit(1)
    if subprocess.run(["git", "check-ignore", "-q", str(expected_dir)], cwd=REPO_ROOT).returncode != 0:
        sys.stderr.write(f"Error: refusing to clear {expected_dir}: it is not git-ignored\n")
        sys.exit(1)
    shutil.rmtree(expected_dir, ignore_errors=True)
    expected_dir.mkdir(parents=True)

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

    ghidra_version = ghidra_path.name
    extensions_dir = ee_extension.user_extensions_dir(ghidra_version)
    if not ee_extension.is_installed(extensions_dir):
        sys.stderr.write(
            "CRITICAL BLOCKER: the Emotion Engine Ghidra extension is not installed.\n"
            "Stock MIPS languages truncate functions at R5900 instructions. Run:\n"
            "  python3 tools/install_ghidra_ee_extension.py\n"
        )
        sys.exit(1)

    if not DATA_REGION_SCRIPT.exists():
        sys.stderr.write(f"Error: Data region script missing: {DATA_REGION_SCRIPT}\n")
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
    cmd = build_ghidra_command(analyze_headless, ghidra_proj_dir, elf_path, exporter_script, toml_config, csv_map)
    subprocess.run(cmd, check=True)

    if not csv_map.exists() or not toml_config.exists():
        sys.stderr.write("Error: Ghidra export script did not produce expected output files!\n")
        sys.exit(1)

    guardrail_record = enforce_function_map_guardrail(csv_map, elf_path)

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
    reset_generated_output(output_cpp_dir)
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
        "data_region_script": str(DATA_REGION_SCRIPT.relative_to(REPO_ROOT)),
        "data_region_script_sha256": compute_sha256(DATA_REGION_SCRIPT),
        "function_map_guardrail": guardrail_record,
        "ghidra_language": ee_extension.LANGUAGE_ID,
        "ghidra_extension_sha256": ee_extension.installed_digest(extensions_dir),
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
