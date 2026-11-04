# HUD Drawing Artifacts: Evidence and Investigation

This document tracks the visible corruption around the mission resource bar and
minimap frame. The earlier version attributed it to GS CLUT swizzling and
`4.2k` asset packing without evidence from the affected draw. That explanation
was not established and has been removed.

## Reproduced behavior

The desktop campaign scenario reproduces the cyan, green, and brown corruption
shown in the Switch captures. With the opt-in shape trace enabled, the affected
minimap decoration is loaded as `n_icomap.slp#56260` and drawn through
`func_0025E640` into area `0x599d08`. That area is a 640×448 surface with a
1280-byte row pitch and CT16 pixels. The observed draw requests include frames
2, 4, 6, 8, 10, and 12 around `(405,320)`–`(578,398)`. The resource bar
corruption is also visible in the reproduction and includes PS2 shape
`main1.shp#56281`.

These draws pass through the game's software shape rasterizer
`func_0017e350`; the PS2Recomp guest implementation dispatches its row-span
decoders through guest function pointers. The available trace logs shape IDs,
frames, coordinates, and area layout, but does not capture input span data or
the destination pixels before and after each draw. Therefore the first corrupt
stage has not yet been isolated.

## Current conclusions

- The corruption is reproducible in the guest software shape-drawing path.
- The trace does not show that the affected shapes use a GS texture or CLUT.
- Existing GS CSM1/CSA tests do not establish a connection to these software
  rasterized shapes.
- The `4.2k` container description is not evidence that its frame commands are
  decoded incorrectly.
- Do not treat a fix to GS CLUT swizzling or a new HUD renderer as a correction
  until a capture shows that stage is responsible.

## Next evidence needed

Capture the destination area immediately before and after a draw of
`n_icomap.slp#56260`, together with the selected row-span decoder and the
source bytes it reads. Compare the resulting pixels with the same asset's
expected frame data. Apply the correction at the first stage where those values
diverge, without editing generated C++ in place. If the divergence is in
PS2Recomp-generated execution, fix the generator/runtime or add a verified
host override and regression coverage.

The existing reproduction command is:

```bash
AOE2_SHAPE_TRACE=20000 AOE2_SHAPE_TRACE_DRAWS=10000 \
  python3 tools/drive_desktop_scenario.py campaign \
  --binary build/docker-desktop-diagnostics/aoe2_game \
  --card-root /tmp/aoe2-artifact-trace-mc --label artifact-trace
```

The shape trace is a pass-through diagnostic; it does not replace the game's
drawing behavior. See [`high-level-draw-seams.md`](high-level-draw-seams.md) for
the verified drawing boundaries and [`hd-render-evidence.md`](../hd-render-evidence.md)
for the existing runtime evidence.
