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

### Two-point calibration (1920×1080)

`AOE2_LOG_POINTER=1 python3 tools/drive_desktop_scenario.py calibration --resolution 1920x1080`
drives the campaign into the mission and then places the host pointer at the
window positions of logical points (100, 120) and (520, 380).
`tools/check_pointer_calibration.py` reports a settled guest-cursor error of
**(0, 0)** for both points. In the capture of the second point, the cursor tip is at
about window (1383, 915) against the predicted (1382, 916), and the game shows its
"Mini-map" tooltip for the hovered panel.

## Playfield buffer source

With patch `0009` reporting large GIF DMA payloads, the 640×262 PSMT8 playfield
upload comes from guest RAM `0x01F98C80` (10,480 quadwords, twice per frame). A
394×198 surface upload comes from `0x01FC2800`, the fixed surface address passed
by the decomp's `func_001889a0` (`../aoe` `src/render/render_manager_surface.cpp`).

## Playfield buffer writers

`PS2X_WRITE_WATCH=0x1F98C80-0x1FC1C80 PS2X_WRITE_WATCH_SUMMARY=20000` (patch
`0011`) aggregates CPU stores into the playfield buffer by guest pc. In the mission,
all writes come from three byte-pixel span copiers (generated `FUN_001536a0`,
`FUN_00154310`, `FUN_00153450`). The span rasterizer `func_0017e350` calls them
(`../aoe` `src/trade/trade_action_stream_raster.cpp`, commit `c3233fd4f`). It walks a
per-row span list and clips against a global clip rectangle. That rasterizer sits
below shape identity: the shape/frame/player parameters must be captured at its
callers.

## Shape identity at the draw seam

The decomp's `func_0025E640(shape, area, x, y, frame, flags)` is the SLP shape-draw
entry (`src/render/render_helper_2184D0.cpp`), and it calls the span rasterizer.
`func_0025D6D0(shape, name, resource_id)` loads shapes. With `AOE2_SHAPE_TRACE=20000`
(diagnostic `game_hooks/src/aoe2_shape_trace.cpp`, pass-through observers), the
mission draws report the loader's resource ids. Examples: `scr2.shp#50100`,
`cam.shp#50232`, `cam8.shp#50242`, `g_grs.shp#15001`, `g_sn2.shp#15019`, unit or
flag graphics `#2296`, `#4652`, `#3578`–`#3580`, and PS2-only ids in the 56xxx range
(`main1.shp#56281`, `n_icomap.slp#56260`, `dc_memo.shp#56110`). Some names read as
`valid`, so the second argument is not always the file name; the ids are
consistent.

**The PS2 resource ids match the PC numbering (hypothesis, supported by 1341 shared ids
with equal or near-equal frame counts).** The local HD Edition install has
`drs/graphics/2296.slp`, `4652.slp`, `3580.slp` and `drs/interface/50100.slp`,
`50232.slp`, `50242.slp`. It has no 56xxx files (PS2-specific) and no 15xxx SLPs:
HD terrain is under `terrain/` in another format.

**CORRECTION (2026-10-28) — the PS2 sprites are smaller than the HD ones.**
`game/SLES-50282/ALL_DATA.BIN` is a standard DRS container (`1.00tribe`, one `slp `
table, 1347 entries; the decomp's `docs/audio_and_asset_pipeline.md` says the same).
Its shapes use a PS2 variant tag `4.2k` (header: tag, u32 frame count, then 16-byte
frame entries `{u32 cmd, u32 outline, u16 w, u16 h, ...}`). Comparing frame sizes
with the HD Edition files for the 1341 ids present in both: **HD/PS2 median size
ratio is 1.58 (width) and 1.59 (height)**, and 629 of them have the same frame
count. Example: id 2296 frame 0 is 53×35 on the PS2 and 85×56 in the HD files; id
4652 is 74×89 versus 119×143. The PS2 build shipped PC art scaled by about 0.625;
the HD files are the full-size PC art. The earlier "no gain" reading, which used
only the 800×600 interface screens, was wrong for unit, building and terrain
sprites. Interface screens still differ anisotropically (800/640 = 1.25 vs
600/448 = 1.34), so only small, aspect-matching HUD pieces qualify.

## Draw areas (map view size)

The trace also dumps each draw area passed to the shape draw. The mission uses two:

| Area | Buffer | Width × height | Pitch | Clip |
| :--- | :--- | :--- | :--- | :--- |
| screen | (16-bit) | 640 × 448 | 0x500 | (0,0)–(639,447) |
| map view | `0x01F98000` | 640 × 262 | 640 | (0,0)–(639,261) |

The map view panel creates its area at initialization. It takes one from the area
pool, then calls the area initializer with the panel's own width and height (panel
`+0x14`/`+0x18`) and the fixed buffer `0x01F98000`. The visible map size is
therefore the panel rectangle. A PC-like larger view needs three things: a larger
panel rectangle, a buffer big enough for it, and a presentation path that does not
go through the GS (whose texture width limit is 1024). The current buffer is
followed at `0x01FC2800` by another surface. Space for a larger buffer has not been
found yet.

## Shape object and data format

A shape object is 0x14 bytes: `+8` points at the data and `+0` holds its size. The
data is **not** SLP 2.0N. Two layouts were seen in the mission:

- Raw frame tables: `00 00 00 00 | frame count (u16) | 01 00 | offset table ...`,
  followed by per-frame width/height fields (e.g. a full-screen 640×448 frame and
  small interface frames). This is the PS2's own packed shape format.
- Data starting with the ASCII tag `4.1k` or `4.2k` (two variants of one container).

Shape draws carry the arguments `(shape, area, x, y, frame, flags)`; x/y can be
negative (off-view origin) and flags were 0 in every sampled draw. Replacing a
drawn shape by an HD file therefore needs a decoder for the PS2 shape format as
well as the id mapping. The span rasterizer is the pixel writer, so an exact
oracle is available: replay the draws through it and compare with the playfield
buffer.

## Map view size experiment (`AOE2_MAP_VIEW_OVERRIDE=320x130`)

Setting the map panel to 320×130 at its initializer (guest `0x31a410`, panel `+0x14`/`+0x18`)
changes the rest of the pipeline consistently in the mission:

- the draw area becomes 0x140 × 0x82 (pitch 0x140) and the rasterizer window follows;
- the playfield upload drops from 640×262 to 320×130 (120 transfers per 30 frames);
- the GIF payload drops from 10,480 to 2,600 quadwords.

The captured mission frame (`.playwright-mcp/campaign-mission.png`) shows the terrain
clipped to the top-left 320×130 region at the same origin. Units outside that rectangle
disappear; the resource bar, command panel, minimap and text are unchanged.

So the map view size is driven by the panel rectangle. Enlarging the view is
therefore a buffer problem: the current buffer is 640×272 with a 5-row guard band and
ends at `0x01FC2800`, so a larger view needs a relocated buffer and free guest RAM.

## 4.2k pixel commands: blocked

The frame table is confirmed (`assets/src/slp4k_reader.cpp`, commit `f835d4d`). The
command table is not. Assuming the SLP 2.0N layout (a u32 per row at `cmd_offset`,
each giving a data-relative start for that row's commands) fails on the real data:
for id 2296 frame 0 (53×35) the row values are non-monotonic and exceed the 6,264-byte
shape; for id 3580 they are in the tens of millions. So the row table is not
data-relative in the same way, and the command bytecode cannot be decoded yet. The
decomp's 2.0N rasterizer (commit range `55d6ddf6c`–`938131ba9`) cannot be applied
directly until the 4.2k row addressing is known.

## Consequences for the HD plan

1. Presentation and pointing share one transform (Phase 1): done in code; see
   the validation document for the run results.
2. Playfield HD requires capturing the software blitter's shape draws (Phase 2).
   The writers of the 8-bit playfield buffer must still be identified (write-watch,
   call trace).
3. Text and HUD can be captured at the KCET quad/upload boundary. The upload's
   source in guest RAM identifies the element; the GS staging address does not.
