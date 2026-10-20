# Age of Empires II: The Age of Kings — PS2 Native Recompilation Port

> **Current State Notice:**
> **Bootstrap phase only.** This repository contains only the architectural foundation, target validation metadata, interface stubs, and dependency management scaffolding. There is **no playable port claim, no active recompilation runtime, no game assets, and no emulator included.**

---

## 1. Project Objective & Non-Goals

### Objective
To build a clean, reproducible, and legally sound foundation for a native recompilation port of **Age of Empires II: The Age of Kings** (PlayStation 2 PAL edition, serial `SLES-50282`), with **Nintendo Switch homebrew as the primary target**. Desktop x86-64/Linux is the validation host where the port is developed and tested first, followed by an ARM64 portability gate and then the Switch backend.

The PS2 release supports a USB mouse; on Switch that mouse input is replaced by the **touchscreen** (tap = left click, long press = right click, drag = box select, two-finger drag = pan) through a high-level input adapter rather than USB/IOP emulation. Docked, a USB mouse and keyboard plugged into the dock work like the PS2 USB peripherals. See `docs/architecture.md` §6.

### Explicit Non-Goals
- No distribution or inclusion of copyrighted game code, binary executables, ISO images, or proprietary assets.
- No inclusion or tracking of PS2Recomp-generated C++ code in git history.
- No full software emulation of the PS2 Graphics Synthesizer (GS) or I/O Processor (IOP).
- No asset upscaling, HD modding, or gameplay mechanic alterations during the recompilation bootstrap.
- No Switch homebrew builds or cross-compilation during the initial desktop bootstrap phase.

---

## 2. Legal, Safety, and Provenance Rules

- **Clean-room provenance**: This repository is strictly independent. It does not import source code, headers, documentation, or symbols from any unofficial or proprietary disassembly projects.
- **User-supplied game assets**: Original game executables (`AOK.ELF`) must be legally owned and provided locally by the user.
- **Zero game asset distribution**: The `.gitignore` configuration strictly forbids committing `.ELF`, `.BIN`, `.ISO`, or extracted game data.
- **Third-Party Licensing**:
  - `PS2Recomp` is licensed under the **GNU General Public License v3.0 (GPL-3.0)**. Recompilation tools and any runtime code linking with PS2Recomp components must adhere to GPL-3.0 obligations. Do not make unsupported legal claims regarding license compatibility without consultation.
  - `libnx` is licensed under the **ISC License**.
  - `deko3d` is licensed under the **Zlib License**.
- Third-party repositories are managed via `tools/bootstrap_dependencies.py` and tracked in `third_party_provenance.lock.json`. Their source trees are ignored in git.

---

## 3. Host Prerequisites

- **Linux x86-64** (Ubuntu 22.04+ or equivalent)
- **C++20 compliant compiler** (`g++` >= 11 or `clang++` >= 13)
- **CMake** (>= 3.20) and **Ninja**
- **Python** (>= 3.10)
- **Git**
- **External Prerequisite for Function Map Export:**
  - **Ghidra** (>= 10.x): Required for running PS2Recomp's `ExportPS2Functions.java` headless analyzer script. Must have `GHIDRA_HOME` configured in your environment.

---

## 4. Repository Layout

```
aoe2-ps2-recomp/
├── CMakeLists.txt                 # Host build definition
├── third_party_provenance.lock.json # Tracked commit SHAs and licenses
├── core/                          # Target metadata & verification specifications
│   ├── target.py                  # Python target definition
│   └── include/aoe2/core/         # C++ target constants & neutral types
├── runtime/                       # Guest memory & execution adapters
│   ├── include/aoe2/runtime/      # GuestMemory, PlatformServices
│   └── src/                       # Runtime implementations
├── platform/                      # Host platform implementations
│   └── desktop/                   # Desktop Linux x86-64 platform layer & smoke test
├── input/                         # Platform-neutral input actions & interfaces
│   ├── include/aoe2/input/        # Pointer/menu actions, touch gestures, USB mouse/keyboard, guest adapters
│   └── src/                       # Action utilities
├── renderer/                      # Graphics abstraction interfaces
│   ├── include/aoe2/renderer/     # IRenderer, FramebufferInfo
│   └── src/                       # NullRenderer (headless/test capture)
├── tools/                         # Maintenance and verification scripts
│   ├── import_owned_game.py       # Checksum-verified ELF importer
│   ├── verify_owned_game.py       # Local game validation tool
│   ├── bootstrap_dependencies.py  # Dependency cloner & lockfile manager
│   └── validate_ghidra_env.py     # GHIDRA_HOME validation script
├── docs/                          # Documentation & technical specifications (docs/sot: prior art & reuse rules)
│   └── architecture.md            # System layers and technical rationale
└── tests/                         # Automated unit & behavioral tests
```

---

## 5. Bootstrap, Validation, and Workflow Commands

### Step 1: Bootstrap Third-Party Dependencies
Clone external repositories (into ignored `third_party/`) and generate/verify the lockfile:
```bash
python3 tools/bootstrap_dependencies.py
# Or check status without modifying files:
python3 tools/bootstrap_dependencies.py --check
```

### Step 2: Import Legally Owned Game Executable
Import your locally dumped PAL executable:
```bash
python3 tools/import_owned_game.py --source /path/to/SLES-50282/AOK.ELF
```
*Expected target:*
- File: `game/SLES-50282/AOK.ELF`
- Expected SHA-1: `f478e19f2059d52899b691483ef5c7522e30e17c`

### Step 3: Verify Imported Game Binary
```bash
python3 tools/verify_owned_game.py
```

### Step 4: Run Automated Tests
Run Python test suite:
```bash
python3 -m unittest discover -s tests
```

### Step 5: Configure and Build C++ Host Scaffolding
```bash
cmake -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## 6. Future Workflow: Ghidra Function Map Export

Before PS2Recomp can translate MIPS R5900 instructions to C++, a symbol and function map must be exported using Ghidra:

1. Validate your Ghidra environment:
   ```bash
   python3 tools/validate_ghidra_env.py
   ```
2. Open the verified `game/SLES-50282/AOK.ELF` in Ghidra with the PS2/MIPS R5900 language profile.
3. Execute PS2Recomp's headless script:
   ```
   third_party/PS2Recomp/tools/ExportPS2Functions.java
   ```
4. Output the resulting symbol and function map to an untracked directory: `analysis/generated/`.
5. Review the map for correctness before feeding it to PS2Recomp.
