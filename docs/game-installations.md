# Game Installations and Asset Storage Layout

This document describes how owned game editions and binary assets are stored, organized, and tracked in this repository, following clean-room provenance and git-ignore rules.

All binaries, game discs, archives, and extracted data files reside strictly inside the root `game/` folder, which is excluded from source control by `.gitignore`.

---

## 1. Directory Structure

```
game/
├── SLES-50282/          # Primary static-recompilation target: PS2 PAL disc and ELF
├── hd-edition/          # Reference & optional HD UI/widescreen asset source (2013 HD v5.8)
└── classic-pc/          # Reference PC CD-ROM release (1999 Age of Kings RTM + community patches)
```

---

## 2. Edition Breakdown

### 2.1 PS2 PAL Release (`game/SLES-50282/`)

- **Platform**: Sony PlayStation 2 (PAL region)
- **Serial / Disc ID**: `SLES-50282`
- **Main Executable**: `AOK.ELF` (SHA-1: `f478e19f2059d52899b691483ef5c7522e30e17c`, 4,076,904 bytes)
- **Role in Project**: Primary static-recompilation guest binary.
- **Contents**:
  - `AOK.ELF`: Stripped EE MIPS binary analyzed via Ghidra.
  - `SYSTEM.CNF`, `SLES_502.82`: Boot and disc configuration.
  - `ALL_DATA.BIN`, `DATA.CAT`: Disc file archive and catalog.
  - `MODULES/`, `MODULES2/`: IOP driver IRX modules.
  - `kcet_hooks.ini`: Local resolved function addresses for KCET hardware overrides.
- **Tooling**: `tools/import_owned_game.py`, `tools/import_owned_disc.py`, `tools/verify_owned_game.py`.

---

### 2.2 HD Edition (`game/hd-edition/`)

- **Platform**: PC (Steam / 2013 Edition)
- **Exact Version**: Age of Empires II HD Edition v5.8
- **Role in Project**: Source of higher-resolution assets and widescreen UI/HUD frames (see `docs/hd-assets.md` and `docs/architecture.md` §7.3).
- **Contents**:
  - `archive/`: Original archive (`Age.of.Empires.II.HD.v5.8.zip`).
  - `install/`: Extracted installation (`Age.of.Empires.II.HD.v5.8`).
  - `source/`: Filtered asset categories (loose numeric files like SLP 2.0N, WAV, BINA).
  - `catalog.json`: Local asset index mapping numeric IDs, format types, sizes, and SHA-256 hashes.
  - `inventory.txt`: Summary of asset categories and dimensions.
- **Tooling**: `tools/import_hd_assets.py`, `tools/verify_hd_assets.py`, `tools/inventory_hd_assets.py`.

---

### 2.3 Classic PC Release (`game/classic-pc/`)

- **Platform**: PC (Windows 95/98/NT)
- **Original Archive**: `Age of Empires II.rar` (226 MiB, SHA-256: `9fa3910c0e35fa84d128df0ddcf628bc7fa7393fe37cf1f40aa9128f73fce850`)
- **Exact Base Version**:
  - **Product**: *Age of Empires II: The Age of Kings* (RTM Release)
  - **Main Executable**: `empires2.exe` (PE32 Intel i386)
  - **File Version (`FileVersion`)**: `00.14.14.0914` (Built September 14, 1999)
  - **Product Version (`ProductVersion`)**: `2.0`
  - **SHA-1 of `empires2.exe`**: `9ad7fdd5c294cafff60862a3cbbe18999118c570`
- **Data Format**: Original monolithic `.drs` archives (`Data/interfac.drs`, `Data/graphics.drs`, `Data/terrain.drs`, `Data/sounds.drs`, `Data/gamedata.drs`) and `Data/empires2.dat`.
- **Bundled Community Additions**:
  - Brazilian community localization package from *AoE World* (`aoeworld.cjb.net`):
    - Subfolder `Aoew/`: `Aoewpatchbr.exe`, `Ajuda.exe`, `CamCopy.exe`, `Tc.exe`, `Leiame.doc`, `Traduções.doc`.
    - Local translation and launch helpers (`RunAok.exe`, `DesPatch.exe`).
- **Layout in Repository**:
  - `archive/`: `Age of Empires II.rar` (unmodified compressed archive).
  - `install/`: Extracted folder `Age of Empires II/` containing the full game tree.
- **Role in Project**: Reference data for original PC engine assets, legacy DRS archive layouts, DRS vs loose asset comparisons, and reference behavioral verification.
