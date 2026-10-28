# HD rendering evidence: presentation geometry and frame composition

Measured 2026-10-27 with the desktop diagnostic build and
`PS2X_GS_FRAME_STATS=30` (runtime patch `0009-gs-frame-stats`), driving
`tools/drive_desktop_scenario.py campaign --resolution 1920x1080` into the first
William Wallace mission. Numbers are per 30 presented frames unless noted.

## Display registers (stable from the menus into the mission)

| Register | Value | Decoded |
| :--- | :--- | :--- |
| PMODE | `0x8067` | both read circuits enabled |
| SMODE2 | `0x1` | interlaced, field mode |
| DISPFB1/2 | `0x11400` / `0x11450` (alternating) | FBP 0 / 80, FBW 10 (640 px), PSMCT16; DBX = DBY = 0 |
| DISPLAY1 | `0x1ff9ff01847290` | DX 656, DY 71, MAGH 3, MAGV 0, DW 2559 → 640 px, DH 511 → 512 lines |

The runtime therefore presents a double-buffered **640×512** PSMCT16 frame.

## Frame composition in gameplay

- The game composes its **640×448 logical screen** in a PSMCT16 work buffer
  (FBP 256). It then copies that buffer into the display buffer with one textured
  strip covering (−0.5, −0.5)–(639.5, 511.5). The logical screen is **stretched
  vertically by 512/448 and fills the whole presented frame**. This matches the
  decomp's `RenderManager` screen setup (640×448, `../aoe` `func_001847c0`) and the
  capture, where the HUD reaches the bottom of the 512-line frame.
- **Playfield:** composed in software. Each frame uploads two 640×262 PSMT8 images.
  Textured sprites draw them at (0, 40)–(640, 302), between the resource bar and the
  command panel. Terrain, units, and the in-world cursor therefore reach the GS as
  pixels, with no per-asset identity. The semantic seam for the playfield is the
  game's software blitter, not GS textures.
- **Text:** each glyph is uploaded separately as a small PSMT4 image (about 6×6 to
  18×11) and drawn as a sprite; about 75 glyph sprites per frame in the mission.
- **HUD pieces and overlays:** PSMT8 uploads through the same staging area (one
  40×80 image per frame, a 396×198 image occasionally), drawn as textured strips.
  There are also a few untextured triangles and lines for fills/outlines.

Gameplay is therefore **hybrid**. The playfield is a software-composed 8-bit
image. Text and HUD elements are small per-element uploads drawn as quads through
the KCET packet path. A GS-level texture hash would identify only the HUD/text
uploads, which all reuse one staging address. A software-blitter seam is required
for the playfield.

## Cursor coordinates

The routine that stores the cursor position steered by the host mouse hook
clamps x to 0..628 and y to 0..431 (`0x274` / `0x1AF` in the generated code). The
cursor therefore lives in the 640×448 logical screen. The decomp's 0..479 clamp is a
different routine. The host mapping is window → aspect-fit 640×512 frame →
640×448 logical (`aoe2::input::PresentationTransform::LogicalFillsFrame`;
`tools/presentation_geometry.py` for the scenario driver).

## Consequences for the HD plan

1. Presentation and pointing share one transform (Phase 1): done in code; see
   the validation document for the run results.
2. Playfield HD requires capturing the software blitter's shape draws (Phase 2).
   The writers of the 8-bit playfield buffer must still be identified (write-watch,
   call trace).
3. Text and HUD can be captured at the KCET quad/upload boundary. The upload's
   source in guest RAM identifies the element; the GS staging address does not.
