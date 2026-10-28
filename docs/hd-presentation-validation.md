# HD presentation: regression diagnosis and remaining gates

## Status (2026-10-27)

Native HD/GLES3 presentation is **not complete**. The desktop runner presents the
CPU-rendered guest framebuffer through raylib. The current diagnostic desktop
build uses `GRAPHICS_API_OPENGL_33`, not an established GLES3 context.
`GlesRenderer` command recording and successful HD asset decoding do not prove
that native-resolution sprites or text reach the presented frame.

The unverified post-frame HD HUD overlay has been removed. Native KCET packet
handling remains enabled; the complete guest framebuffer is the safe fallback.
The installer reports that fallback rather than claiming an active HD renderer.
This restores the existing presentation path, not the intended native HD result.

## Root causes of the visual regression

- The host callback drew opaque bands **after** the complete guest framebuffer.
  It covered the resource display and lower gameplay area without redrawing any
  dynamic values, buttons, selected-unit information, or minimap.
- Interface asset numbers were used without a verified PS2-to-HD semantic map.
  Successful decoding of `50102` with `50588` did not establish a gameplay HUD.
  The resulting decoration resembled loading-screen parchment.
- Cropped source bands were scaled differently horizontally and vertically.
  Matching their destination to guessed guest rectangles did not preserve their
  proportions or make them correspond to live controls.
- A separate, fixed 1920×1080 input mapper was installed even when no native HUD
  was visible. Its hit regions differed from the overlay's draw rectangles.
  That installation and the unused global mapper pointer have been removed.

Changing another palette, sampler wrap mode, or resource number is not a fix for
these architectural problems. Do not paint decorative imagery over a composed
PS2 frame and call it a functional native HUD.

## Presentation geometry: unresolved discrepancy

The authorized decomp's render manager configures a logical 640×448 screen.
The current GS presentation logs report 640×512 frames. The host draw loop
aspect-fits the reported frame dimensions; the host mouse and scenario driver
use the logical 640×448 extent. This explains why their independently calculated
rectangles disagree, but does **not** establish a valid crop offset.

The backend reads from `DISPFB.DBX/DBY` and derives dimensions from `DISPLAY`.
`PresentationFrame` does not carry a separate logical active rectangle. Verify
that rectangle against actual scanout/register data before cropping. Ultimately,
drawing and pointing must consume the same verified presentation transform.
Do not force 448 rows or modify GS register semantics based only on a screenshot.

## Regression evidence

`tests/test_gpu_presentation_fallback.py` compiles the production GPU installer
byte-for-byte with runtime/graphics boundary doubles and the real asset decoder,
provider, resolver, and layout code. Synthetic SLP/palette inputs ensure that
missing local HD assets cannot silently skip the regression. With the old
installer, the callback issues draws over the guest HUD, installs an unrelated
input mapper, and reports native HD rendering. The three assertions fail against
`0ced1c4` and pass with the fallback correction. This is a draw-boundary test,
**not GPU pixel-readback evidence**.

`tests/test_runtime_patches.py` now also asks Git to parse the tracked patch
series and, when the local dependency exists, verifies its applied state.
This exposed two reproducibility defects: patch `0007` had invalid hunk counts
and indentation; patch `0005` omitted the `ps2xRuntime/` path prefix. Both patch
files are corrected without resetting or editing the dependency checkout.

Validation of this correction:

- Lightweight C++ suite: **44/44 passed**.
- Python suite: **348 tests, OK, 2 skipped**.
- Owned-code native GPU differential test: **1/1 passed**.
- All **8 runtime patches** verify as applied; reapplication is a no-op.
  Reversing the complete series on an isolated copy, applying it in order, and
  reapplying restored all 7 affected source files byte-for-byte with no further
  changes. This verifies the current patched source copy, not a fresh bootstrap
  of every dependency from the lockfile.
- Docker diagnostic desktop build succeeded. The campaign was driven with
  `python3 tools/drive_desktop_scenario.py campaign --resolution 1920x1080 --keep-card`.
  Inspection of actual `campaign-mission.png` shows the original resource bar
  and minimap unobscured, without the decorative top/bottom overlay. The inspected
  single-player menu and briefing captures also have no such overlay.
- Selection/movement, return-to-menu transitions, 720p, GLES3 pixel readback, and
  Switch hardware were **not validated** in this correction.

The lightweight C++ suite and native GPU differential test are regression gates,
not visual acceptance criteria. A campaign driver's zero exit code likewise
means the scripted process survived; screenshot inspection establishes only the
visible observations recorded above, not full gameplay or HD acceptance.

## Gates before enabling a native HD replacement

1. Trace a real semantic draw seam with resource identity and verify each asset's
   role and palette. A GS command kind alone does not identify a HUD element.
2. Preserve or reconstruct all dynamic HUD contents and their ordering. Do not
   suppress the guest path until the replacement can produce the entire region.
3. Publish owned scene/layout snapshots across the game/host thread boundary.
   Do not classify scenes by reading unsynchronized, potentially stale pointers.
4. Use a shared, resize-aware presentation and input transform at 1080p and 720p.
   Activate relocated hit regions only while those regions are actually drawn.
5. Implement real texture/quad submission and font rasterization on the thread
   owning the graphics context. Verify the API/context rather than logging GLES.
6. Inspect actual pixels during loading, menus, briefing, gameplay, and return to
   menus. Exercise selection, movement, commands, and minimap input. Keep capture
   evidence in ignored `.playwright-mcp/`; do not commit assets or screenshots.

Switch hardware and GLES3 pixel output remain unvalidated.
