# Implementation handoff: GS-independent graphics

This plan implements the [canonical rendering contract](../sot/high-level-draw-seams.md#7-gs-independent-presentation-contract); it is indexed by [docs/README.md](../README.md). Its final goal is boot through gameplay without GS visual processing. Preserve the legacy reference during development. Do not edit generated C++, weaken provenance checks, or change the owner's decomp repository. Runtime changes belong in the authorized PS2Recomp fork; record a new pin and truthful generation provenance when required.

## Starting point and rules

- Switch's host frame provider currently emits a map-only frame or waits with black output. Its optional UI snapshot calls into `m_gs`; desktop's current compositor takes a GS frame. Neither is a GS-independent complete renderer.
- The map surface hooks, arena reservation, compositor and inverse input layout exist, but mission parity and expanded terrain are not accepted. The [viewport plan](expanded-viewport-native-implementation.md) contains the map-specific gates and latest evidence. Its minimap-versus-playfield finding supersedes earlier submission claims.
- Build on the current runtime frame owner and presentation context. Use real PS2 art and software map rasterization for the first complete implementation. Do not make HD assets, new fonts, native GPU sprite rendering, or expanded viewport a prerequisite for original-size coverage.
- Complete the tasks in order. Each task records exact tested revision, platform, configuration, commands, outcomes, visual evidence and known gaps in the appropriate existing checkpoint/SOT. An unavailable seam is a blocker for that content, not permission to fabricate a layout or declare success.

## 1. Inventory and frame-boundary proof

**Inputs:** `game_hooks/src/map_view_session.cpp`, `game_hooks/src/map_view_observation.cpp`, runtime presentation and GS call sites, owner decomp, existing navigation/viewport checkpoints.

1. Run a diagnostics-enabled legacy baseline through boot, player creation/selection, main menu, campaign narrative, mission, pause/dialog, and return to menu. Capture actual frames plus matching screen, draw/submission and timing traces. Preserve memory-card data and identify binary hash and revision. Record failures and scenarios that cannot be reached.
2. For each visual item in the canonical coverage table, inventory the producer, guest object/resource lifetime, source format and palette, placement, clip, order, changes per frame, and downstream GS dependencies. Distinguish direct evidence from hypotheses. Investigate `func_0021A960`, shape loader/draw and font paths without assuming one seam captures everything.
3. Trace entry, yield/resume and completion of guest drawing, including duplicate submissions and transitions. Identify a completed-frame boundary and a host synchronization point. Trace VSYNC, interrupts and waits separately; document the behavior the replacement must preserve.

**Gate:** a reproducible coverage matrix with sample captures and a verified frame boundary for each implemented scenario. If the boundary, palette or a source cannot be established, keep that scenario incomplete and investigate before implementing its replacement.

## 2. Native frame and diagnostics foundation

**Depends on:** task 1 frame-boundary evidence. **Primary areas:** runtime presentation boundary, `game_hooks`, `renderer`.

1. Introduce a bounded per-generation capture/assembly contract as specified in the SOT. Sealed data owns copies or retained immutable resources; order, format, clip, palette, lifetime and output dimensions are explicit. Validate bounds and overflow. Abort incomplete or stale generations on transition, resource failure or shutdown.
2. Route the resulting owned RGBA frame through the existing presenter. Extend the actual desktop and Switch runtime paths as needed; do not create a second EGL context or use the GS-derived desktop compositor as the native source. Implement independently verified frame pacing and guest-visible completion before disabling the corresponding GS side effects.
3. Add an explicitly selected strict native mode and named diagnostics for unsupported/missing draw, palette, source, frame completion, stale state, overflow and timeout. Strict mode fails rather than returning `nullopt` to a legacy/black fallback. Keep reference mode separately selectable. Instrument or disable every GS visual entry used during strict tests; assert zero native-path GS accesses, including UI capture and palette lookup.
4. Test generation ownership and synchronization with synthetic producers, including guest memory mutation after capture, out-of-order or mixed generations, panel destruction, resize, capacity limits, failure and shutdown. Verify that a failed frame never publishes a new input layout or stale frame.

**Gate:** synthetic complete frames reach the existing presenter on desktop and Switch without GS access; missing content fails visibly and deterministically. This gate does not claim real game coverage.

## 3. Boot and menus

**Depends on:** tasks 1–2 and a proven source for each operation. Capture images, surface updates, clears, clips and transitions before GS processing, retaining original resources where appropriate. Implement the smallest verified operation set; retain and report unsupported operations as failures in strict mode. Preserve frame order and native PS2 colors/geometry.

**Gate:** inspect boot, loading, player selection/creation, main menu and campaign navigation side by side with matched legacy captures; verify animated transitions and return paths, using strict-mode zero-GS evidence. A visible loading screen or log marker alone is insufficient.

## 4. HUD, text, cursor and overlays

**Depends on:** task 3 and the source/ordering inventory. Capture original UI surfaces and glyph data before GS; preserve transparency, color, clipping, metrics and draw order across HUD, minimap, messages, dialogs, tooltips and cursor. Cover changes and overlapping elements, not only static backgrounds. Integrate the transform used for rendering with pointer hit tests; do not introduce moved HUD regions without the corresponding inverse mapping.

**Gate:** compare changing counters, selections, minimap, overlapping text/dialogs and cursor against legacy captures; exercise mouse/touch and controller navigation. Tests cover clipping, palette/alpha, layering and input boundaries. Strict mode reports unsupported content instead of showing partial UI as success.

## 5. Map and integrated mission

**Depends on:** tasks 2 and 4. Complete the existing viewport plan's playfield-identity, palette and frame-timing gates. First compose the original-size software map with native HUD in proven guest order. Preserve map pixels already containing units and fog. Then validate the opt-in expanded viewport, safe arena ownership, input mapping, scrolling, transitions and 720p/1080p output. Keep expanded mode opt-in until accepted. Do not substitute sprites until original-draw suppression and ordering are independently proven.

**Gate:** actual mission captures show revealed terrain, hero/objects, HUD, text, minimap and cursor; exercise selection, scrolling, pause and exit to menu. Verify both legacy-size parity and expanded output, and ensure minimap traffic is not mistaken for the playfield. Visual evidence and zero-GS instrumentation must come from the same native strict run.

## 6. Platform acceptance and rollout

- Run focused behavioral tests for formats, alpha, clipping, order, frame ownership, failure paths and inverse input transforms, followed by `python3 -m unittest discover -s tests` and the relevant Debug CMake/CTest suites (`cmake --build ... -v`, `ctest --output-on-failure -V`). Use the full desktop probe only via `python3 tools/run_desktop_probe_build.py --jobs N`.
- Build Debug Switch using `python3 tools/docker_build.py --platform switch`; execute the repository's applicable desktop and Switch scenario drivers with diagnostics and capture complete logs. Inspect actual frames on desktop and Switch at handheld 720p and docked 1080p; test resize or dock transition where supported. If a scenario is blocked by runtime, input, assets or environment, record the precise blocker and do not mark the gate passed.
- In strict runs, verify zero access to GS visual processing, including initialization and transitions; demonstrate guest pacing and progress with GS visual execution disabled. Check frame lifetime, pacing, memory bounds and that no stale input layout survives screen transitions. Compare images against legacy reference while accounting for explicitly documented platform presentation differences.
- Make GS-independent presentation the default only after every required scenario and platform gate passes. Keep the legacy path available for diagnosis until its removal is separately justified. Update the SOT and checkpoints as gates pass; do not describe tests of a mock, screenshot of a loading screen, or a successful build as gameplay acceptance.
