# Switch manual navigation checkpoint

The Switch runtime now prohibits GS framebuffer presentation and accepts
host-owned native frames. An experimental producer converts captured native
map snapshots without a GS framebuffer; its menu and HUD regions are black,
and no real Switch map frame has yet been validated. When no native snapshot
is available, presentation is black. Native menu and HUD producers are still
required. The run described below predates this change.

The guest's `func_0021A960` receives source and destination rectangles and a
source viewport before it emits drawing commands. The owner's decompilation
(`src/render/render_viewport_21a960.cpp`) documents this boundary. It is a
candidate for native interface composition, but texture formats, palettes,
clipping, ordering, fills, text and frame boundaries have not been validated;
intercepting it alone must not be described as complete menu or HUD support.

2026-10-31. This is an incomplete investigation, not a gameplay pass. The
existing NRO (`build/docker-switch/aoe2_game.nro`, SHA-256
`5899b2d51d76ba2a3835e7771deb1ac0e3c08d2ef5bdfef055d2f6818f1adb0a`)
was launched in Ryujinx in a separate Docker container with an isolated
`build/switch-manual/ryujinx-data` root and `mode=native`. No scenario driver
was running. The actual hash should be checked against the local artifact
before comparing runs.

The captured screen was still `NOW LOADING...` around 50 seconds after launch.
Around 100 seconds it showed Select Player with `---KB`; later it showed
`8190KB` and New Player. A short Z press while the card was busy did not
visibly advance. A later four-second Z press preceded the New Player dialog;
because the card check also completed in that interval, this does not isolate
the control as the cause. Brightness alone cannot establish card readiness.
The OSK and a name field containing `a` were subsequently observed, but
keyboard and controller mappings overlap, so the input path was not isolated.
Captures and the detailed runtime log are local ignored artifacts under
`build/switch-manual/`.

## Controller-only investigation

A subsequent Debug NRO was rebuilt with `[host-pad] switch buttons` logging
on changes (SHA-256
`8c022297d11c015c5ae91848aa1aadd472a05259d908863ad99a74fdbd313b99`).
A separate Ryujinx root at `build/switch-control-only/ryujinx-data`
used `WindowKeyboard` as the controller backend with the emulated USB keyboard
and mouse disabled. The screenshots in `build/switch-control-only/` and its
`run.log` are local evidence. This run still used the GS framebuffer path and
therefore is **not** native rendering or gameplay acceptance.

* With the ready card showing `8190KB`, Z produced Switch B (`0x2`) and opened
  New Player. B on the selected name field opened the on-screen keyboard; B on
  its initial `a` inserted that letter. Down moved through its rows, then two
  Right presses moved from SPACE through SPECIAL to ENTER; B opened the name
  confirmation. A later B confirmed it and the game reached the main menu.
* B opened Single Player, then Campaigns. From the initial Campaign Menu
  selection, five Up presses selected William Wallace; B opened its scenario
  map. Down selected the first mission, and B opened its narrative.
* A few 0.5-second direction presses were not recorded in `[host-pad]` on this
  slow run. Two-second holds with four-second release intervals were observed
  reliably in the sampled steps; they are observations, not a universal timing
  guarantee. Screen transitions sometimes took more than 30 seconds.
* The first narrative page stayed on screen for over two minutes after the
  log showed `1 s-txt` and its MP3 name. B and Plus were recorded by the pad
  logger but no `1 e-txt` or map submission appeared during that interval.
  The audio backend fell back to Dummy; the cause of the stalled narrative is
  not yet established. Do not assume a press or an audio failure caused it.

The current automatic driver still uses mouse clicks and unverified fixed
sequences. Do not treat the observations above as an idempotent replay: state
recognition, the existing-card path, narrative completion, and native rendering
must be implemented and validated separately.

The configuration maps keyboard Z to Switch B and enables the emulated USB
keyboard too. The runtime logged USB keyboard Z events during those presses.
Consequently prior mixed keyboard/controller trials cannot establish controller
operation. Repeat with USB keyboard and mouse emulation disabled and verify
individual controller states before deriving a menu sequence. Account for a
fresh and an existing memory card separately.

The same run logged `[map-view-install] mode=native`, but also logged
`presentation: guest framebuffer fallback` and `GS::latchHostPresentationFrame`.
Setting the map-view mode is therefore insufficient to prove an entirely native
presentation path. The map was not reached or visually verified. The existing
driver's mixed clicks, fixed waits and repeated unverified actions remain to be
replaced after the controller and native rendering paths are established.
