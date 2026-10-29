> [!IMPORTANT]
> **CONSOLIDATED DOCUMENTATION NOTICE**
> This document is historical/fragmented. Its architectural conclusions, tooling commands, and evidence have been unified into the canonical Source of Truth: [`docs/sot/hd-assets-strategy.md`](docs/sot/hd-assets-strategy.md). Please refer to `docs/sot/hd-assets-strategy.md` for current decisions and implementation status.

---

# Source of Truth: AoE2 HD Edition (2013) Asset Resolution vs. PS2 and Definitive Edition

## 1. Overview & Historical Context

A common architectural question in the recompilation of *Age of Empires II: The Age of Kings* (PS2 PAL `SLES-50282`) is why the assets extracted from **Age of Empires II: HD Edition (2013)** have the exact same dimensions as the 1999 PC CD-ROM release, rather than being "high-resolution" sprites (e.g. 1080p or 4K native).

The name **HD Edition (2013)** (developed by Hidden Path Entertainment / Skybox Labs / Microsoft on Steam) referred to **engine modernization and viewport expansion**, **not** to redrawn unit or building spritesheets:

* **1999 PC Original (*The Age of Kings*)**: Designed by Ensemble Studios for 4:3 CRT monitors. Menu interface assets were authored at **800×600** pixels (`50100.slp`), and gameplay typically ran at 800×600 or 1024×768. Units were rendered as 2D isometric pixel sprites (e.g. ~85×56 px for infantry/cavalry).
* **2013 HD Edition**: Modernized the Genie Engine for Windows 7/8/10, added Steam Workshop, and introduced widescreen/arbitrary resolution support. Crucially, **the underlying unit and interface SLP assets were retained from the 1999 original**. Playing at 1080p or 1440p in HD Edition simply expanded the camera's visible map viewport (revealing more terrain at 1:1 pixel scale), leaving sprites at their original 1999 dimensions.
* **2019 Definitive Edition (DE)**: This version was the only release that completely overhauled the artwork, replacing the legacy 2D sprites with newly rendered, 4K-native 3D-generated spritesheets and dynamic zoom levels.

---

## 2. Technical Evidence in the Codebase

Inspecting the assets in `game/hd-edition/` and comparing them against the PS2 PAL disc (`game/SLES-50282/`) confirms this reality:

| Asset Type | Resource ID / Name | HD Edition (2013) Resolution | PS2 PAL (`SLES-50282`) Resolution | Notes |
| :--- | :--- | :--- | :--- | :--- |
| **Interface Base** | `50100.slp` | **800×600** (1 frame) | **800×600** (downscaled to 640×448) | Main menu / setup background |
| **Interface Large** | `50102.slp` | **1280×1024** (1 frame) | N/A (PS2 uses 50100 scaled) | High-res menu variant on PC |
| **Unit / Flag** | `2296.slp` | **85×56** px (frames 82×53..85×56) | Loaded via `func_0025D6D0` | Standard infantry / unit scale |
| **Unit Graphic** | `4652.slp` | **119×143** px (frames 107×162..119×143) | Identical resource ID | Cavalry / large unit scale |
| **Unit Graphic** | `3580.slp` | **54×128** px (16 frames) | Identical resource ID | Standard building / unit element |
| **Logical Screen** | Gameplay Frame | Arbitrary (e.g. 1920×1080 viewport) | **640×448** (stretched to 640×512 PAL) | Playfield clipped to 640×262 at y=40 |

### Key Findings
1. **Identical Resource IDs**: The PS2 port uses the exact PC resource numbering scheme (`func_0025D6D0(shape, name, resource_id)`). Resource IDs such as `50100` (interface) and `2296`, `4652`, `3580` (units) match 1:1 with PC SLP files.
2. **PS2 Adaptations**: The PS2 version adapted the game for CRT televisions and limited EE RAM (32 MB) by restricting the logical framebuffer to **640×448**, with a software-rasterized playfield of **640×262** rendered into guest RAM at `0x01F98C80`.

---

## 3. Architectural Impact & Engineering Decision

Because the HD Edition 2013 assets are PC originals (800×600 menus and ~85×56 px unit frames), **simply replacing PS2 shape assets while keeping the camera locked to a 640×448 viewport produces negligible visual improvement** (at most the 800×600 vs. 640×448 ratio, not true 1080p sharpness).

Therefore, as formalized in `docs/plans/nifty-herding-pudding.md`:
* **Widescreen Viewport Expansion**: Real high-definition rendering (on Desktop and Nintendo Switch) is achieved by **expanding the engine's map viewport** to match the native window/display resolution (1280×720 handheld / 1920×1080 docked/desktop), presenting the original sprites at a 1:1 pixel ratio across a wider field of view.
* **HUD Recomposition**: The HUD panels (top resource bar and bottom command panel) are decoupled and anchored to the screen borders, while the playfield expands to fill the widescreen space, exactly as the PC version functioned.
* **Definitive Edition Assets & Guest RAM**: For detailed architecture analysis regarding why DE 32-bit RGBA assets reside in Host GPU VRAM and how guest RAM expansion (64 MiB to 128 MiB+) benefits simulation scale (pop cap 200+, ludicrous maps) rather than texture storage, see [`docs/sot/hd-assets-strategy.md`](hd-assets-strategy.md) §6.
