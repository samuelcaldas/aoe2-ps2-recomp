# HUD Rendering Artifacts, CLUT Swizzling & GLES Modernization Roadmap

This document serves as the canonical Source of Truth (SOT) explaining the root causes behind visual artifacts observed on HUD buttons in the native recompilation of *Age of Empires II: The Age of Kings* (PS2 PAL `SLES-50282`), and details the architectural roadmap to resolve them via the host OpenGL ES 3 / deko3d renderer ([`IRenderer`](../../renderer/include/aoe2/renderer/renderer.hpp)).

This document directly connects to [`CLAUDE.md`](../../CLAUDE.md), [`docs/README.md`](../README.md), [`docs/adr/0001-kcet-boundary-replacement.md`](../adr/0001-kcet-boundary-replacement.md), and [`docs/sot/subsystems-assets-evolution.md`](subsystems-assets-evolution.md).

---

## 1. Visual Manifestation & Identification of Affected Elements

During scenario and mission execution (e.g. `python3 tools/drive_desktop_scenario.py campaign`), the captured game frame ([`.playwright-mcp/campaign-mission.png`](../../.playwright-mcp/campaign-mission.png)) reveals distinct digital noise (multicolored "static" and jagged diagonal banding in cyan, yellow, green, and brown) on specific interface regions:

```
+-----------------------------------------------------------------------------------+
| [Wood] [Food] [Gold] [Stone] [Pop 1/0] | Dark Age | [ ARTIFACTS: Menu/Dipl/Chat ] |  (y = 0..40)
+-----------------------------------------------------------------------------------+
|                                                                                   |
|                                                                                   |
|                          PLAYFIELD (640x262 / 1280x534)                           |
|                                                                                   |
|                                                                                   |
+-----------------------------------------------------------------------------------+
| [Unit Actions / Wood] |  [Parchment / Stats]  |  [ ARTIFACTS: Minimap Buttons ]   |  (y = 574..720)
|                       |                       |      [ Minimap Radar Diamond ]    |
+-----------------------------------------------------------------------------------+
```

### Affected Elements
1. **Top Resource Bar (Far Right)**:
   - System and social buttons: **Menu**, **Diplomacy**, and **Chat**.
   - Backed by the PS2-exclusive interface resource [`main1.shp#56281`](../hd-render-evidence.md#L96).
2. **Bottom Command Panel (Four Corners of Minimap)**:
   - Radar mode toggle buttons (**Combat Mode**, **Economic Mode**, **Normal Mode**).
   - Signal/Flare button.
   - Decorative stone bevel trims surrounding the $45^\circ$ rotated minimap diamond.
   - Backed by the PS2-exclusive interface resource [`n_icomap.slp#56260`](../hd-render-evidence.md#L96).

### Unaffected (Correctly Rendered) Elements
- **Font & Numerical Glyphs**: Text like `"Dark Age"`, `"0"`, `"1/0"` is rendered cleanly from the 4-bit font atlas texture (`font-t4` at base `10432`) using its dedicated, pre-isolated CLUT (`clut-8060`).
- **Panel Backgrounds**: Tiled wood texture (`back-t8`) and parchment use static 8-bit textures with the global interface palette (`clut-8000`).
- **Resource Icons**: Food (meat), Wood (logs), Gold (gold pile), Stone (bricks), and Population (villager) are shared PC SLP shapes from `graphics.drs`, rasterized via standard byte-pixel span copiers (`FUN_001536a0`, `FUN_00154310`).
- **Minimap Interior**: The black diamond and unit dots are rendered via software by `RenderManagerSurface` ([`func_001889a0`](../hd-render-evidence.md#L73-L75)) directly onto a 394×198 buffer.

---

## 2. Root Cause Analysis

The distortion on the buttons stems from a combination of **PS2-exclusive asset packing** and **Graphics Synthesizer (GS) CLUT swizzling mismatch**:

### A. PS2-Exclusive 56xxx Asset Series & `4.2k` Compression
- The classic 1999 PC release and the 2013 HD Edition do not contain assets in the `56xxx` numerical ID range. 
- KCET created `main1.shp#56281` and `n_icomap.slp#56260` specifically for console gamepad navigation.
- These shapes are stored in `ALL_DATA.BIN` with a proprietary KCET container tag **`4.2k`** (16-byte frame descriptors with non-monotonic command bytecode), distinct from standard PC SLP 2.0N files.

### B. GS Memory Organization & CLUT Swizzling
- On the PS2 GS, indexed textures (4-bit `PSMT4` and 8-bit `PSMT8`) do not map colors linearly from VRAM. The GS bus architecture requires colors in a 256-color palette to swap address bits 3 and 4:
  $$\text{swizzled\_idx} = ((i \gg 5) \ll 5) \mid (((i \gg 4) \& 1) \ll 3) \mid (((i \gg 3) \& 1) \ll 4) \mid (i \& 7)$$
- The KCET engine performs this swizzling in software before DMA transfer ([`render_palette_helper_21b440.cpp`](file:///home/samuelcaldas/repos/aoe2-ps2-recomp/third_party/age-of-empires-2-decomp/src/render/render_palette_helper_21b440.cpp#L38-L46)) into destination `0x1F00`.
- The software GS backend ([`GSCpuBackend::LookupCLUT`](file:///home/samuelcaldas/repos/aoe2-ps2-recomp/third_party/PS2Recomp/ps2xRuntime/src/lib/gs/gs_cpu_backend.cpp#L1040-L1078)) attempts to invert this swizzling. However, for 4-bit textures (`PSMT4`), color lookup depends on the `csa` (*CLUT Sub-table Address*) field selecting one of 16 sub-palettes of 16 colors each.
- When partial CLUT updates occur, or when `csa` strides are misaligned with the software-swizzled layout in `GSCpuBackend`, indices $0..15$ map to wrong colors or uninitialized memory, producing the diagnostic diagonal noise bands.

---

## 3. Architectural Policy: Do Not Patch Legacy GS

Per [`ADR 0001`](../adr/0001-kcet-boundary-replacement.md) and [`docs/sot/prior-art-and-reuse.md`](prior-art-and-reuse.md):
> **We do not invest engineering effort reverse-engineering low-level PS2 GS eDRAM emulation quirks or fixing complex CLUT swizzle bugs.**
> The PS2 hardware layer (`kt_gpu`) is designed to be stripped and superseded by a high-level host renderer (`aoe2::renderer::IRenderer`) that presents truecolor 32-bit RGBA textures.

The UI logic has already been cleanly extracted and decompiled from the original code:
- [`hud_panel_layout.cpp`](file:///home/samuelcaldas/repos/aoe2-ps2-recomp/third_party/age-of-empires-2-decomp/src/render/hud_panel_layout.cpp): Computes exact bounding boxes, button matrices, and hit testing for PC 640x480, PC 800x600, and PS2 640x448.
- [`hud_resource_bar.cpp`](file:///home/samuelcaldas/repos/aoe2-ps2-recomp/third_party/age-of-empires-2-decomp/src/render/hud_resource_bar.cpp): Manages string formatting (food, wood, gold, stone, population) and button click hit-testing (Menu, Diplomacy, Chat).
- [`hud_command_bar.cpp`](file:///home/samuelcaldas/repos/aoe2-ps2-recomp/third_party/age-of-empires-2-decomp/src/render/hud_command_bar.cpp): Manages the 3×5 action button grid, HP percentage bars, and unit portraits.

---

## 4. Next Steps for the GLES Modernization Pipeline

The modern renderer is defined in [`aoe2::renderer::GlesRenderer`](../../renderer/include/aoe2/renderer/gles_renderer.hpp) implementing [`IRenderer`](../../renderer/include/aoe2/renderer/renderer.hpp). The concrete roadmap to complete GLES presentation and eliminate HUD artifacts consists of the following 5 stages:

### Stage 1: Implement Textured Quad Pipeline in `GlesRenderer`
* **Current Status**: [`GlesRenderer::DrawSprite`](../../renderer/src/gles_renderer.cpp#L211-L225) records draw commands into `current_frame_commands_` but lacks the actual GL draw call (`// Draw textured quad using VBO / shaders`).
* **Next Action**:
  1. Compile an embedded shader pair (vertex shader for 2D orthographic projection and UV pass-through; fragment shader for sampled 2D texture with tint modulation).
  2. Create persistent Vertex Buffer Objects (VBO) and Element Buffer Objects (EBO) for dynamic batching of 2D textured quads.
  3. Execute `glDrawElements` during `EndFrame()` or per-batch flush.

### Stage 2: Desktop EGL / GLES3 Context Activation
* **Current Status**: On Nintendo Switch, EGL connects directly to Horizon's `nwindow` surface (`nwindowGetDefault`). On desktop Linux (`AOE2_HAS_GLES`), [`InitGlesContext`](../../renderer/src/gles_renderer.cpp#L68-L74) only sets `gl_context_active_ = true` without creating an active EGL/GLES context.
* **Next Action**:
  1. Add desktop EGL headless/surfaceless or X11/Wayland window context binding, enabling actual OpenGL ES 3.0 / Mesa rendering on the Linux development host.
  2. Bind `eglSwapBuffers` inside [`EndFrame()`](../../renderer/src/gles_renderer.cpp#L135-L139).

### Stage 3: High-Level HUD Draw Interception (`kt_gpu`)
* **Current Status**: [`game_hooks/src/kcet_gpu_module.cpp`](../../game_hooks/src/kcet_gpu_module.cpp#L26-L29) prints `[aoe2] presentation: guest framebuffer fallback; native HD HUD unavailable`.
* **Next Action**:
  1. Connect `SetNativeDrawInterceptor` in [`gpu_runtime_adapter.hpp`](../../game_hooks/src/gpu_runtime_adapter.hpp#L18-L20) to intercept the game's HUD draw requests.
  2. Feed decoded button frames from loose assets or decompiled vector shapes into `UploadTexture` as 32-bit RGBA textures.
  3. Emit `DrawSprite` calls for Menu, Diplomacy, Chat, and Minimap buttons at their resolved coordinates from `hud_panel_layout.cpp`.

### Stage 4: Native Font & Text Rasterization
* **Current Status**: `DrawText` records text commands into `current_text_commands_` without drawing glyphs.
* **Next Action**:
  1. Implement glyph blitting using a pre-baked TrueType font atlas or `stb_truetype`.
  2. Draw resource counts and Age titles using native crisp vector text instead of scaling 4-bit bitmap fonts.

### Stage 5: Visual Regression Verification
* **Gate**: Run `python3 tools/drive_desktop_scenario.py campaign --resolution 1280x720` and `1920x1080`.
* **Acceptance Criteria**:
  - Center: Edge-to-edge expanded playfield (1280×534 / 1920×708) with zero distortion.
  - Top Bar: Pristine Menu, Diplomacy, and Chat buttons anchored at $y=0..40$ with zero noise artifacts.
  - Bottom Bar: Pristine Minimap mode buttons and stone borders anchored at $y=574..720$.
  - Captures verified in `.playwright-mcp/` without committing image binaries.
