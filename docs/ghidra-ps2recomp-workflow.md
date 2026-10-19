# Ghidra and PS2Recomp Workflow Specification

## 1. Overview & Objectives

This document establishes the reproducible, legally compliant workflow for extracting function maps from the validated PlayStation 2 PAL executable of **Age of Empires II: The Age of Kings** (`SLES-50282` / `AOK.ELF`) using **Ghidra** and compiling the function boundaries into static C++ code using **PS2Recomp**.

> **Crucial Disclaimer:**  
> A successful code generation run is **not** evidence of compilation, linking, bootability, correct hardware emulation (EE/VU/GS/IOP), graphics rendering, or Nintendo Switch compatibility. It represents solely an intermediate offline translation from MIPS machine instructions into static C++ statements.

---

## 2. Prerequisites and Validation

Before running the workflow, the following prerequisites must be verified:

1. **Validated Original ELF**:
   - Location: `game/SLES-50282/AOK.ELF` (git-ignored).
   - Expected SHA-1: `f478e19f2059d52899b691483ef5c7522e30e17c`.
   - Verify via:
     ```bash
     python3 tools/verify_owned_game.py
     ```

2. **PS2Recomp Toolchain**:
   - Repository: `third_party/PS2Recomp` (pinned to commit `eb8688b2fd191129f103a39543002c005b33409c`).
   - Binaries built in `third_party/PS2Recomp/build/`:
     - `ps2xRecomp/ps2_recomp`
     - `ps2xAnalyzer/ps2_analyzer`
   - Verified via:
     ```bash
     python3 tools/bootstrap_dependencies.py --check
     ```

3. **Ghidra Installation & Java Runtime (External Prerequisite)**:
   - Ghidra version: >= 10.x (Java 17+ or Java 21+).
   - `GHIDRA_HOME` environment variable must be set and exportable:
     ```bash
     export GHIDRA_HOME=/path/to/ghidra_11.x_PUBLIC
     ```
   - Verify via:
     ```bash
     python3 tools/validate_ghidra_env.py
     ```
   - **Current Blocker Notice**: Ghidra is an external prerequisite and must be installed by the developer/operator on the host system. The workflow halts cleanly if `GHIDRA_HOME` or `analyzeHeadless` is absent.

4. **Storage Space**:
   - At least 10 GB of free space is recommended under the working directory for generated C++ multi-file output and Ghidra project files.

---

## 3. Directory Layout & Git Hygiene

All generated disassembly, Ghidra project files, exported CSV/TOML files, and generated C++ sources are strictly git-ignored to comply with copyright laws and repository hygiene rules.

| Directory | Purpose | Git Status |
| :--- | :--- | :--- |
| `game/SLES-50282/AOK.ELF` | User-provided original PAL executable | **IGNORED** |
| `analysis/ghidra/` | Temporary headless Ghidra project directory | **IGNORED** |
| `analysis/generated/` | Exported function maps (`function_map.csv`, `config.toml`) | **IGNORED** |
| `generated/SLES-50282/` | Generated C++ recompilation source files | **IGNORED** |
| `generation_manifest.json` | Small tracked metadata and provenance record | **TRACKED** |

Confirm that all generated paths are ignored:
```bash
git check-ignore -v analysis/ghidra/SLES-50282.gpr analysis/generated/function_map.csv generated/SLES-50282/
```

---

## 4. Exact Execution Workflow

### Step 1: Headless Ghidra Analysis and Function Export

Using Ghidra's `analyzeHeadless` processor, we import `game/SLES-50282/AOK.ELF` with the PS2 MIPS R5900 language profile (`MIPS:LE:64:64-32addr`) and run the exporter script provided in the pinned PS2Recomp tree: `third_party/PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java`.

```bash
mkdir -p analysis/ghidra analysis/generated

$GHIDRA_HOME/support/analyzeHeadless \
    analysis/ghidra \
    SLES_50282_Project \
    -import game/SLES-50282/AOK.ELF \
    -processor "MIPS:LE:64:64-32addr" \
    -scriptPath third_party/PS2Recomp/ps2xRecomp/tools/ghidra \
    -postScript ExportPS2Functions.java \
    analysis/generated/SLES-50282.toml \
    analysis/generated/function_map.csv
```

### Step 2: Minimal PS2Recomp TOML Configuration

PS2Recomp accepts a TOML configuration specifying the ELF input, Ghidra CSV function map, and output folder.
A minimal clean configuration without speculative stubs, skip lists, or instruction patches is placed at `analysis/generated/SLES-50282.toml`:

```toml
[general]
input = "game/SLES-50282/AOK.ELF"
ghidra_output = "analysis/generated/function_map.csv"
output = "generated/SLES-50282"
single_file_output = false
low_memory_mode = false
patch_syscalls = false
patch_cop0 = true
patch_cache = true
stubs = []
untracked_stubs = []
skip = []
```

### Step 3: Run PS2Recomp Code Generation

Execute the compiled `ps2_recomp` binary:

```bash
mkdir -p generated/SLES-50282
./third_party/PS2Recomp/build/ps2xRecomp/ps2_recomp analysis/generated/SLES-50282.toml
```

This decodes the R5900 machine code for each discovered function boundary and emits multi-file C++ functions into `generated/SLES-50282/`.

### Step 4: Record & Verify Generation Provenance Manifest

After successful generation, create/update `generation_manifest.json` recording only high-level, legal metadata:

```json
{
  "target_id": "SLES-50282",
  "elf_sha1": "f478e19f2059d52899b691483ef5c7522e30e17c",
  "ps2recomp_commit": "eb8688b2fd191129f103a39543002c005b33409c",
  "ghidra_version": "<version>",
  "exporter_script": "ps2xRecomp/tools/ghidra/ExportPS2Functions.java",
  "timestamp": "<iso-timestamp>",
  "exported_function_count": 0,
  "generated_cpp_file_count": 0,
  "artifact_paths": [
    "analysis/ghidra/SLES-50282.gpr",
    "analysis/generated/function_map.csv",
    "analysis/generated/SLES-50282.toml",
    "generated/SLES-50282"
  ]
}
```

Verify compliance and git-ignore safety:
```bash
python3 tools/verify_generation_manifest.py
```

---

## 5. Fallback Analyzer Workflow (Local Offline Bring-Up)

If Ghidra is temporarily unavailable in the local environment, the PS2Recomp project embeds an internal static heuristic analyzer (`ps2_analyzer`):

```bash
mkdir -p analysis/generated
./third_party/PS2Recomp/build/ps2xAnalyzer/ps2_analyzer \
    game/SLES-50282/AOK.ELF \
    analysis/generated/SLES-50282_analyzer.toml
```

*Note*: `ps2_analyzer` uses jump-and-link (`JAL`) heuristics and the embedded Sony Computer Entertainment (SCE) SDK database. While useful for local verification of the pipeline, **Ghidra function export remains the authoritative primary workflow** for retail stripped ELFs.

---

## 6. Known Blockers & Next Steps

1. **`GHIDRA_HOME` Dependency**: Ghidra must be installed on the host to generate the authoritative Ghidra function map.
2. **Next Task**:
   Once generation succeeds, proceed to:
   > *"Compile the generated output with a minimal desktop runtime probe and classify the first compiler/runtime blockers without adding game-specific stubs."*
