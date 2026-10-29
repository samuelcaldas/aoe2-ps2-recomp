# Native map view: reservation and baseline checkpoint

Status: **partial implementation; native map mode is not available**. This checkpoint belongs to the [native map view plan](plans/nifty-herding-pudding.md); see the [documentation index](README.md) and [presentation strategy](sot/hd-assets-strategy.md). A reserved buffer region and a larger host window do not establish viewport expansion.

## Legacy campaign replay (2026-10-29)

Executed without either map-view environment variable:

```bash
env -u AOE2_MAP_VIEW_OVERRIDE -u AOE2_MAP_VIEW \
  python3 tools/drive_desktop_scenario.py campaign --resolution 1920x1080 --keep-card
```

The driver exited successfully. Its final capture, `.playwright-mcp/campaign-mission.png`, was inspected: the first William Wallace tutorial mission is visible, with terrain and a blue flag, the resource bar, Dark Age indicator, bottom command region, minimap, tutorial text, and mouse cursor. Resource counters are zero and population reads 1/0. No explicit camera-scroll command was issued; numerical camera coordinates and actual selection/orders were not measured. This is a legacy screenshot, not evidence of additional visible terrain.

`--keep-card` avoids the driver's recursive deletion of the existing memory-card directory. It means the replay preserves the local card state rather than proving a fresh-card boot.

The driver ran `build/docker-desktop-diagnostics/aoe2_game`, with the owned ELF and ISO from `game/SLES-50282/`, setting `DISPLAY=:99`, `AOE2_WINDOW_WIDTH=1920`, and `AOE2_WINDOW_HEIGHT=1080`. No inherited `AOE2_*` or `PS2*` variables were present. The executable was 167,989,024 bytes, with SHA-256:

```text
3bc1bb712ad25755af736d5629a73835bb502e99c97afdc5013eeb93151973ff
```

**This existing executable predates the reservation changes below.** Its source revision was not embedded/established. Do not describe this replay as gameplay validation of the newly modified runtime. The owned ELF separately passed its size/SHA-1 check.

## Implemented reservation boundary

The fork reserves an exclusive 8 MiB interval directly below HLE:

| Region | Start (MiB) | Exclusive end (MiB) |
| :--- | ---: | ---: |
| Heap's maximum extent | configured base | 54.75 |
| Map reservation | 54.75 | 62.75 |
| HLE pools | 62.75 | 63 |
| Callback stacks | 63 | 64 |

`PS2_MAP_ARENA_BASE` and `PS2_MAP_ARENA_SIZE` derive from the existing RAM/HLE constants. Both the runtime's initial heap limit and heap reconfiguration stop at the reservation. `SetupHeap` and `EndOfHeap` use the same ceiling, including the latter's no-runtime fallback. ELF loads reject an intersecting segment before copying or clearing that segment, including cached/uncached aliases. HLE and callback-stack locations are unchanged.

The runtime owns this reserved address interval independently of heap blocks. **No surface allocator, module lease/lifetime API, buffer relocation, or enlarged draw area has been implemented.** Nothing currently claims or writes the interval on behalf of the map. This completes only the reservation-boundary portion of plan step 2.

Tests were observed failing first for heap allocation into the future reservation and for ELF writes into it. After the boundary change and the independent review fix for syscall fallback limits, the fork's complete test runner passed 489/489 from the fork root. The new tests cover repeated heap reconfiguration, final valid heap allocation, rejection of allocation into the reservation, sentinel preservation, exact ELF boundary, a crossing segment, and physical/KSEG aliases. An initial run from the parent repository also failed the existing VU enum-source lookup test because that test requires the fork as its working directory; rerunning from the fork root resolved that unrelated failure without changing the test.

## Contract investigation: not an activation gate pass

The authorized reference checkout was read-only at commit `83a0f2f7581750291287aad76336129b73fdb98a`. It does not supply implementations of all three panel/area initialization routines named in the plan; generated instructions remain the authority for those routines.

Targeted inspection establishes that area reconfiguration can request ten additional rows, and scanline setup can start five pitches after the backing buffer. It also builds a row-address table and has resumable loop checkpoints. This is evidence for further contract tests, **not** a validated larger-surface allocator. Alternate pixel formats, all margins/clear paths, visibility grids, and screen-to-world consumers still need executable fixtures and validation.

The active map CLUT has **not** been identified. TEX0/TEX2 select and load CLUT state; index arrangement, CLUT pixel format, TEXA, texture/vertex color combination, and alpha all affect the result. A presumed interface palette is not a substitute. The earlier two-upload observation also does not establish whether both map uploads use identical contents at their respective submission times. A map-time trace connecting source buffers, active draw state, and palette data is still required.

HUD placement must use the existing logical-to-frame transform: the logical 640×448 screen fills the PAL 640×512 frame. At 1920×1080, the latter occupies x=285..1635, y=0..1080. The historical logical map band y=40..302 therefore maps to approximately y=96.43..728.04 in the screenshot, not to PAL rows 40..302. This agrees with the inspected legacy image; it does not yet identify every interactive/overlay region for recomposition.

Until those contracts and 640×262 relocation parity pass, do not activate larger dimensions, suppress GS uploads, install a second panel hook, or route out-of-legacy-range coordinates into existing input handlers.

## Provenance and validation limits

At preflight the installed fork/lockfile named `d7ef7a98e2b95dc93731133f8282812850d03918`, while `generation_manifest.json` recorded generation with `a0917444dec12406b86cde27b30a63715c466f99`. The manifest verifier already failed that mismatch. The reservation is committed locally in fork commit `e454f34`; bootstrap recorded its full revision in the lockfile and the installed-dependency check passed. The manifest is not rewritten to imply regeneration, so its verifier still fails the generation/runtime pin mismatch. The fork commit has not been pushed.

`GHIDRA_HOME` was initially unset; an installed Ghidra was found at `/home/samuelcaldas/tools/ghidra_12.1.4_PUBLIC`. Finding it does not itself rerun the guarded pipeline or refresh generation provenance. A full pipeline invocation resets generated output, so it must account for the current overlay-generated units before replacing that output.

No native-mode mission capture, relocation parity comparison, 720p gameplay validation, performance/memory-growth measurement, or Switch visual/input validation has been performed in this checkpoint. No completion criterion for the native viewport is claimed.
