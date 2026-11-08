# Validation scenarios: Single Player → Random Map → Start Game

The automated validators for PC, Nintendo Switch and Android share one UI flow:

`main menu → Single Player → Random Map → Game Settings → player setup → Start Game → loading → map`

The campaign replays (`campaign`, `campaign-auto`) were removed. Requesting them now fails with
`scenario 'campaign' was removed; use random-map-start`.

## Layers

The shared code lives in `tools/scenarios/` (standard library only):

| Pattern | Module | Role |
| :--- | :--- | :--- |
| Command | `steps.py` | Immutable steps: `Tap`, `Hover`, `Press`, `Wait`, `AwaitMarker`. Each has a kebab-case name, a settle time and a `checkpoint` flag. A `Tap` built from an unmeasured point raises `UnresolvedLayoutError`. |
| Template Method | `runner.py` | `ScenarioRunner.run()` calls `boot()` (platform hook), performs steps in order, settles, captures checkpoints, and stops at the first missing effect or exited guest. |
| Strategy | `backends.py` | `InputBackend` implementations. `XdotoolMouseBackend` is the PC backend. The Switch pad and Android touch backends are not written yet. |
| Observer | `recorder.py`, `watcher.py` | `CheckpointRecorder` names screenshots `<platform>-random-map-<step>.png`. `LogMarkerWatcher` waits for a guest log marker such as `[map-submit]`. |
| Factory | `random_map.py` | `random_map_steps(menus)` builds the same flow for every platform. Only the backend differs. |
| Layout | `layout.py` | Every measured logical (640x448) point, with the screenshot that measured it. |

Scenario points are logical pixels. The PC backend maps them with `tools/presentation_geometry.py`
(640x448 logical screen in a 640x512 frame, aspect-fitted).

## Checkpoints

Each step of the flow captures a screenshot after it settles. The files go to
`.playwright-mcp/pc/` (PC), named `pc-random-map-<step>.png`.

| Step | Checkpoint | Expected screen |
| :--- | :--- | :--- |
| `single-player` | after the click | Single Player submenu |
| `random-map-hover` | pointer on row | Random Map row highlighted |
| `random-map` | after the click | Game Settings screen |
| `game-settings` | settled | Game Settings, Map Type list may be open |
| `game-settings-dismiss` | after the neutral click | Map Type list closed |
| `game-settings-ok` | after OK | player setup screen with **Start Game** |
| `player-setup` | settled | player setup, Start Game visible |
| `start-game` | after the click | loading or stalled |
| `loading` | settled | loading screen |
| `map` | after `[map-submit]` | the map |

Passing the automated run does **not** prove the map is visible. `[map-submit]` is a guest log
marker. Acceptance for the map step needs a visual review of the screenshots. The validators must not
report a PASS from the marker alone.

## Platform approach

| Platform | Input | Status |
| :--- | :--- | :--- |
| PC (desktop Linux, Xvfb) | Mouse: xdotool clicks at the measured points | Implemented. Driver: `tools/drive_desktop_scenario.py random-map-start`. |
| Nintendo Switch (Ryujinx) | Pad: D-pad and Cross, and calibrated clicks, as the existing gated driver does | Not migrated. `tools/drive_switch_scenario.py` still has the campaign scenarios. |
| Android (emulator) | Touch: `adb shell input tap` and native `AInputEvent` handling | Not implemented. Requires the native touch path and a live emulator run. |

## Measured points (PC)

Logical points (640x448) in `tools/scenarios/layout.py`. The screenshots measured window pixels;
a 640x448 window letterboxes the 640x512 frame (x scaled by 0.875 and offset by 40, y unchanged), so
the logical values are the inverse of that transform.

| Point | Logical (x, y) | Window pixel measured | Source |
| :--- | :--- | :--- | :--- |
| Single Player | (458, 85) | not re-measured | Earlier investigation capture |
| Random Map | (458, 124) | not re-measured | Earlier investigation capture (row 2, highlighted) |
| Game Settings neutral (closes the Map Type list) | (91, 290) | (120, 290) | Game Settings capture, empty parchment |
| Game Settings OK | (249, 408) | (258, 408), box 212–305 × 396–420 | Game Settings capture |
| Start Game | (289, 409) | (293, 409), box 245–342 × 400–420 | Player setup capture |

## Known blocker: boot state and Start Game

These findings come from runs on 2026-11-08 (PC Release, Xvfb 640x448, label `validation`):

- **The boot sequence does not reliably end on the main menu.** In the capture run, the
  `profile-settle` screen was the player setup lobby ("Click to Start Game"), not the main menu. A
  Single Player click on that screen lands on the lobby's settings panel and opens Game Settings.
  The `single-player` checkpoint in the validation run shows Game Settings, so the Single Player →
  Random Map route was **not** exercised. Nothing in the driver detects the current screen, so the
  run continued regardless. The cause is still unknown. No save state was found on disk outside
  `mc0`, which the driver resets. The next step is to check the boot screen after each boot step
  (a checkpoint per step) and compare it with a run that reached the main menu.
- **Start Game does not advance the game.** The lobby's Start Game click highlights the button and
  the guest stays on that screen. A host backtrace of the guest thread, taken about 40 seconds after
  the click, is inside the host IOP kernel scheduler loop (`IopKernel::beginNextReady`), called from
  the EE scheduler checkpoint. That is consistent with the audio status wait recorded in
  [`sot/kcet-stall-log.md`](sot/kcet-stall-log.md), but it is not proof that they are the same wait.
  The run was stopped manually at that point.

Neither finding is a pass. The map step cannot be accepted until the boot state is reliable and
the Start Game stall is fixed.

## Running

```bash
AOE2_MAP_VIEW=native python3 tools/drive_desktop_scenario.py random-map-start --label <name> --stacks
```

`random-map-start` and `calibration` require `AOE2_MAP_VIEW=native` (or `relocation`), because the
map marker comes from the diagnostic guest TTY. `--stacks` saves host thread backtraces when the run
fails. Evidence goes to `build/desktop-validation/` and screenshots to `.playwright-mcp/pc/`.

## Tests

- `tests/test_scenarios_core.py`: steps, runner order and stopping, marker watcher, recorder, xdotool backend.
- `tests/test_random_map_scenario.py`: one flow for every platform, coordinates taken only from the layout, the unmeasured-point guard, calibration points.
- `tests/test_drive_desktop_scenario.py`: the scenario table, removed names, boot conversion.
