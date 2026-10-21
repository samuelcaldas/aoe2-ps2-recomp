# Game Installations and Asset Storage Layout

This document describes how owned game editions, disc images, binaries, and reference asset repositories are stored, organized, and tracked in this repository, following clean-room provenance and git-ignore rules.

All binaries, game discs, archives, and extracted data files reside strictly inside the root `game/` folder, which is excluded from source control by `.gitignore`.

---

## 1. Directory Structure

```
game/
├── SLES-50282/          # Primary static-recompilation target: PS2 PAL disc, ELF, and ISO
├── hd-edition/          # Reference & HD UI/widescreen asset source (2013 HD v5.8)
├── classic-pc/          # Reference PC CD-ROM release (1999 Age of Kings RTM + community patches)
└── orig/                # Direct archive mirrors imported from matching reference projects
    ├── SLES-50282/      # Original PS2 PAL retail disc ISO (aoe2.iso), ELFs, SYSTEM.CNF
    ├── AGE2_X1_1_0C_EN/ # The Conquerors expansion (PC 1.0c English release)
    ├── AOE2_TRIAL_1999/ # Age of Empires II PC Trial version (1999)
    ├── EMPIRES2_2_0A_EN/# Age of Empires II PC v2.0a English reference
    └── SLPM-62076/      # PS2 NTSC-J (Japan release) reference folder
```

---

## 2. Edition Breakdown

### 2.1 PS2 PAL Release (`game/SLES-50282/`)

- **Platform**: Sony PlayStation 2 (PAL region)
- **Serial / Disc ID**: `SLES-50282`
- **Main Executable**: `AOK.ELF` (SHA-1: `f478e19f2059d52899b691483ef5c7522e30e17c`, 4,076,904 bytes)
- **Role in Project**: Primary static-recompilation guest binary and target.
- **Contents**:
  - `aoe2.iso`: Complete, unmodified PS2 PAL optical disc image (551,100,416 bytes, SHA-1: `081bf584eeea52427a14917454aa9aa9dc66df87`).
  - `AOK.ELF`: Stripped EE MIPS binary analyzed via Ghidra.
  - `SYSTEM.CNF`, `SLES_502.82`: Boot and disc configuration.
  - `ALL_DATA.BIN`, `DATA.CAT`: Disc file archive and catalog.
  - `EN/`, `FR/`, `GR/`, `IT/`, `SP/`: Regional overlay directories and executables.
  - `MODULES/`, `MODULES2/`: IOP driver IRX modules (SPU2, USB mouse, network).
  - `kcet_hooks.ini`: Local resolved function addresses for KCET hardware overrides.
- **Tooling**: `tools/import_owned_game.py`, `tools/import_owned_disc.py`, `tools/verify_owned_game.py`.

---

### 2.2 HD Edition (`game/hd-edition/`)

- **Platform**: PC (Steam / 2013 Edition)
- **Exact Version**: Age of Empires II HD Edition v5.8
- **Role in Project**: Source of higher-resolution assets and widescreen UI/HUD frames (see `docs/hd-assets.md` and `docs/architecture-hd-asset-decoupling.md`).
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
  - **Main Executable**: `empires2.exe` (PE32 Intel i386, `FileVersion 00.14.14.0914`, `ProductVersion 2.0`, SHA-1: `9ad7fdd5c294cafff60862a3cbbe18999118c570`)
- **Data Format**: Original monolithic `.drs` archives (`interfac.drs`, `graphics.drs`, `terrain.drs`, `sounds.drs`, `gamedata.drs`) and `empires2.dat`.
- **Bundled Community Additions**:
  - Brazilian community localization package from *AoE World* (`aoeworld.cjb.net`):
    - Subfolder `Aoew/`: `Aoewpatchbr.exe`, `Ajuda.exe`, `CamCopy.exe`, `Tc.exe`, `Leiame.doc`, `Traduções.doc`.
- **Layout in Repository**:
  - `archive/`: `Age of Empires II.rar`.
  - `install/`: Extracted full game tree.
- **Role in Project**: Reference data for original PC engine assets, legacy DRS archive layouts, and behavioral verification.

---

### 2.4 Reference Repository Mirrors (`game/orig/`)

Directly imported from the reference decompilation repository (`~/repos/aoe/orig/`) to provide full historical parity and behavioral oracle targets:

1. **`game/orig/SLES-50282/`**:
   - `aoe2.iso`: Complete PS2 PAL retail disc image (526 MiB, 551,100,416 bytes).
   - `AOK.ELF`, `AGE2_PS2.ELF`, `SLES_502.82`, `SYSTEM.CNF`.
2. **`game/orig/AGE2_X1_1_0C_EN/`**:
   - *Age of Empires II: The Conquerors Expansion* (Patch 1.0c, August 2001, English).
   - Contains `age2_x1.exe` (SHA-1: `75429c30e70b3074f51e089d71c422faec6d6255`), `age2_x1.icd`, SafeDisc drivers (`secdrv.sys`), `data/empires2_x1_p1.dat`, `data/gamedata_x1_p1.drs`, and official Random Map Script files (`random/ES@*.rms`).
3. **`game/orig/AOE2_TRIAL_1999/`**:
   - Official 1999 pre-release / trial demo version of *Age of Empires II: The Age of Kings*.
   - Contains standalone `EMPIRES2.EXE`, `language.dll`, and trial `.drs` data files (`Data/graphics.drs`, `Data/interfac.drs`, `Data/terrain.drs`, `Data/sounds.drs`, `Data/empires2.dat`).
4. **`game/orig/EMPIRES2_2_0A_EN/`**:
   - Retail reference folder for *The Age of Kings* v2.0a English.
5. **`game/orig/SLPM-62076/`**:
   - Japanese PS2 NTSC-J release identifier and structure placeholder.
