# Native SCmd completion and menu-overlay validation

## Behavior

With the native `cdvd` or `file_cd` module enabled, the SCmd semaphore getter
signals the semaphore stored by the game before returning its ID. This follows
the synchronous host-CDVD completion policy: the subsequent guest wait can consume
the signal. The hook is not installed when both native modules are disabled.
The existing deferred NCmd-completion hook is unchanged.

The overlay loader accepts case-insensitive SMENU and lobby filenames, including
CD-ROM path variants. It writes the `MWo3` header and selects overlay ID 6 or 4,
respectively. Unknown names, unterminated names, and headers extending past guest
RAM return failure without writing a header. Filename scanning is bounded to 256
bytes. The existing generated menu loader already calls this replaced disc-loader
entry; no additional menu-loader or vtable bypass is installed.

The loader selects the registered decompiled code; these tests do not establish
that all overlay data, initialization dependencies, or rendering are functional.

## Verification (2026-10-26)

- The SCmd regression failed on the missing completion signal before the hook was
  added. With the hook, two consecutive signal/wait cycles pass using the real
  scheduler and guest `WaitSema`, with both native enable switches tested
  separately and both-off behavior preserved.
- `ctest --test-dir build/desktop-probe -R cdvd_model --output-on-failure` passed.
- `ctest --test-dir build/desktop-probe -R overlay_dispatch --output-on-failure`
  passed. Coverage includes filename variants, header contents, entry visibility,
  unknown-name rejection, and simulated SMENU/lobby/SMENU transitions through
  `dispatchGuestBranch`. The transition handlers are test sentinels, not execution
  of the complete decompiled menu initialization.
- The unknown-overlay regression was also checked against the original loader:
  it failed on the original unconditional-success result, then passed after
  restoring the updated loader.
- The full desktop-probe CTest run passed all 39 tests. A fresh build without the
  PS2 runtime passed all 37 lightweight tests.
- `python3 -m unittest discover -s tests` ran 327 tests successfully, with 2 skipped.
- `python3 tools/docker_build.py --platform desktop --diagnostics --jobs 4`
  compiled and linked `aoe2_game` successfully on `docker-dev`.
- `python3 tools/validate_desktop.py --run-seconds 30 --interval 10` **failed**
  (exit 1): the process remained running, the classifier reported
  `stalled_in_loop`, no branch targets were missing, and visual progress was not
  detected (`real_frames=0`, `frames_changed=False`).

The diagnostic log confirms installation of the SCmd hook and a signal to the
SCmd semaphore. It also records repeated `CheckDiskMedia failed` messages. No
`Hook_OverlayLoad` request was observed in this run. The three screenshots show
an unchanged **NOW LOADING...** screen, not the menu. Therefore neither actual
SMENU loading during boot nor rendering of the initial menu is verified. The
remaining disc-media failure needs separate diagnosis; no success result or
additional bypass was fabricated to hide it.

## Local evidence (ignored)

- Container build: `build/scmd-smenu-docker-build.log`
- CTest: `build/scmd-smenu-ctest.log`
- Python suite: `build/scmd-smenu-python-tests.log`
- Visual verdict: `build/scmd-smenu-visual-validation.log`
- Game diagnostics: `build/desktop-validation/run.log`
- Screenshots: `.playwright-mcp/desktop-validation-010s.png`,
  `.playwright-mcp/desktop-validation-020s.png`, and
  `.playwright-mcp/desktop-validation-030s.png`

These paths are overwritten by later validation runs; screenshots and game
outputs remain untracked.
