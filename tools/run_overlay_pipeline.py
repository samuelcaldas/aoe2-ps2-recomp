#!/usr/bin/env python3
"""Export and recompile every disc overlay: raw code import, guardrail, generation, registration.

Per overlay (from tools/map_overlays.py): header+text only -> Ghidra headless raw import at the
load address -> function-map guardrail -> ps2_recomp on a synthetic ELF -> overlay registration
and rename header. All outputs are git-ignored; stdout carries counts only.
Requires GHIDRA_HOME and the built third_party/PS2Recomp/build/ps2xRecomp/ps2_recomp.
"""
from __future__ import annotations

import argparse
import csv
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO_ROOT))
from core.target import SINGLE_TARGET  # noqa: E402
from tools import install_ghidra_ee_extension as ee_extension  # noqa: E402
from tools import verify_function_map as guardrail  # noqa: E402
from tools.kcet_resolver.elf_image import load_elf  # noqa: E402
from tools.overlay_entries import entry_targets, seeds_for, switch_targets  # noqa: E402
from tools.map_overlays import HEADER_BYTES, parse_overlay_header  # noqa: E402
from tools.overlay_registration import build_aggregate, build_registration, build_rename_header, parse_register_file  # noqa: E402
from tools.overlay_to_elf import overlay_to_elf  # noqa: E402

ANALYSIS = REPO_ROOT / "analysis" / "generated" / "overlays"
PROJECTS = REPO_ROOT / "analysis" / "ghidra" / "overlays"
GENERATED = REPO_ROOT / "generated" / "overlays"
RECOMP = REPO_ROOT / "third_party" / "PS2Recomp" / "build" / "ps2xRecomp" / "ps2_recomp"
EXPORTER = REPO_ROOT / "third_party" / "PS2Recomp" / "ps2xRecomp" / "tools" / "ghidra" / "ExportPS2Functions.java"


class PipelineError(RuntimeError):
    pass


def first_function_start(csv_text: str) -> int:
    """Lowest start among real functions (continuation labels named entry_* are not callable entries)."""
    starts = [int(row["Start"], 16) for row in csv.DictReader(io.StringIO(csv_text)) if row["Name"].startswith("FUN_")]
    if not starts:
        raise PipelineError("the export contains no functions")
    return min(starts)


def rewrite_toml(text: str, input_path: str, output_dir: str) -> str:
    text, inputs = re.subn(r'(?m)^input = .*$', f'input = "{input_path}"', text)
    text, outputs = re.subn(r'(?m)^output = .*$', f'output = "{output_dir}"', text)
    if inputs != 1 or outputs != 1:
        raise PipelineError("generator config lacks an input or output line")
    return text


def overlay_name(file_name: str) -> str:
    return re.sub(r"\W", "_", Path(file_name).stem.lower())


def run(command: list, log: Path) -> None:
    log.parent.mkdir(parents=True, exist_ok=True)
    with log.open("w") as handle:
        result = subprocess.run(command, stdout=handle, stderr=subprocess.STDOUT)
    if result.returncode != 0:
        raise PipelineError(f"command failed ({command[0]}); see {log}")


SEED_SCRIPT = REPO_ROOT / "tools" / "ghidra" / "SeedFunctions.java"


def export_functions(ghidra_home: Path, name: str, code_bin: Path, base: int, toml: Path, csv_path: Path,
                     seeds: Path) -> None:
    project = PROJECTS / name
    if any(part.startswith(".") for part in project.parts):
        # Ghidra refuses project paths with a dot-prefixed element (e.g. a .worktrees checkout);
        # the project is throwaway, so keep it under the temp dir instead.
        project = Path(tempfile.gettempdir()) / "aoe2-ghidra-overlays" / name
    shutil.rmtree(project, ignore_errors=True)
    project.mkdir(parents=True)
    script_path = f"{REPO_ROOT / 'tools' / 'ghidra'};{EXPORTER.parent}"
    run([str(ghidra_home / "support" / "analyzeHeadless"), str(project), "OV", "-import", str(code_bin), "-overwrite",
         "-loader", "BinaryLoader", "-loader-baseAddr", hex(base), "-processor", ee_extension.LANGUAGE_ID,
         "-scriptPath", script_path, "-postScript", SEED_SCRIPT.name, str(seeds), "-postScript", EXPORTER.name, str(toml), str(csv_path)], ANALYSIS / name / "ghidra.log")


def enforce_guardrail(csv_path: Path, elf_path: Path) -> None:
    policy = guardrail.GuardrailPolicy(guardrail.DEFAULT_MAX_ENTRY_BYTES, guardrail.DEFAULT_MAX_RETURN_GAP_BYTES)
    entries = guardrail.load_function_map(csv_path)
    violations = guardrail.find_violations(entries, guardrail.LoadImage.from_elf(elf_path), policy)
    if violations:
        raise PipelineError(f"function map guardrail found {len(violations)} violation(s) in {csv_path.name}")


def process(entry: dict, raw: bytes, ghidra_home: Path, targets: list) -> dict:
    name = overlay_name(entry["file"])
    work = ANALYSIS / name
    out = GENERATED / name
    shutil.rmtree(work, ignore_errors=True)
    shutil.rmtree(out, ignore_errors=True)
    work.mkdir(parents=True)
    out.mkdir(parents=True)
    header = parse_overlay_header(raw)
    code_bin = work / "code.bin"
    code_bin.write_bytes(raw[:HEADER_BYTES + header.text])
    toml, csv_path = work / "overlay.toml", work / "overlay.csv"
    seeds = work / "seeds.txt"
    entries = set(seeds_for(targets, header.load_address, HEADER_BYTES, header.text))
    entries.update(switch_targets(raw, header.load_address, HEADER_BYTES, header.text))
    seeds.write_text("".join(f"0x{address:x}\n" for address in sorted(entries)))
    export_functions(ghidra_home, name, code_bin, header.load_address, toml, csv_path, seeds)
    first = first_function_start(csv_path.read_text())
    elf_path = work / "overlay.elf"
    elf_path.write_bytes(overlay_to_elf(raw, code_only=True, entry=first))
    enforce_guardrail(csv_path, elf_path)
    toml.write_text(rewrite_toml(toml.read_text(), str(elf_path), str(out)))
    run([str(RECOMP), str(toml)], work / "recomp.log")
    prefix = f"ov{header.overlay_id}_"
    (out / "overlay_rename.h").write_text(build_rename_header((out / "ps2_recompiled_functions.h").read_text(), prefix))
    pairs = parse_register_file((out / "register_functions.cpp").read_text())
    (out / "overlay_registration.cpp").write_text(build_registration(pairs, header.overlay_id, prefix, name))
    return {"name": name, "id": header.overlay_id, "load_address": header.load_address,
            "code_bytes": HEADER_BYTES + header.text, "sources": len(list(out.glob("*_0x*.cpp"))), "entries": len(pairs)}


def main(argv: list | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--only", help="process just this overlay file name")
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)
    ghidra = os.environ.get("GHIDRA_HOME")
    report_path = ANALYSIS / ".." / "overlay_map.json"
    try:
        if not ghidra or not (Path(ghidra) / "support" / "analyzeHeadless").exists():
            raise PipelineError("GHIDRA_HOME must point at a Ghidra installation")
        if not RECOMP.exists():
            raise PipelineError(f"ps2_recomp is not built: {RECOMP}")
        if not report_path.exists():
            raise PipelineError("run tools/map_overlays.py first")
        root = REPO_ROOT / "game" / SINGLE_TARGET.serial / Path(SINGLE_TARGET.disc_elf_path).parent
        overlays = json.loads(report_path.read_text())["overlays"]
        ranges = [(item["load_address"], item["load_address"] + HEADER_BYTES + item["text"]) for item in overlays]
        targets = entry_targets(load_elf(REPO_ROOT / SINGLE_TARGET.relative_dest_path), ranges)
        results = []
        for entry in overlays:
            if args.only and entry["file"] != args.only:
                continue
            try:
                results.append(process(entry, (root / entry["file"]).read_bytes(), Path(ghidra), targets))
            except PipelineError as error:
                raise PipelineError(f"{entry['file']}: {error}") from error
    except (OSError, PipelineError, guardrail.GuardrailInputError, ValueError) as error:
        print(f"Error: {error}", file=sys.stderr)
        return 1
    if not args.only:
        (GENERATED / "overlay_all.cpp").write_text(build_aggregate(results))
    for result in results:
        print(f"{result['name']}: id={result['id']} sources={result['sources']} entries={result['entries']}")
    print(f"Recompiled {len(results)} overlay(s).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
