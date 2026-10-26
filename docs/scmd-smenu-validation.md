# CDVD media contract, completion ownership, and boot validation

## Observed boot outcome (2026-10-26)

With native `cdvd=on`, the diagnostic build now advances beyond the unchanged
**NOW LOADING...** screen: real asset transfers are acknowledged, 23 disc reads
are recorded, and the loading text disappears. The inspected 30-second screenshot
shows a **black screen with a cursor**, not a recognizable menu.

The desktop validator still **fails**: process alive, `stalled_in_loop`,
`real_frames=3`, `frames_changed=False`, and zero missing branch targets. No real
SMENU request, active ID 6 during boot, or SMENU entry execution is established.
This is measurable progress past loading, **not a verified menu or completed boot**.

The outcomes remain separate:

| Milestone | Evidence |
| --- | --- |
| Media contract preserved | Original classifier and mode mapping execute; accepted path observed. |
| Isolated reader strict | Exact synthetic bytes and failure-state regressions pass. |
| Loading-screen progress | Asset read acknowledgments and loading text disappearing in a real run. |
| SMENU reached / menu visible | Not established; black screen with cursor is insufficient. |

## Contract repairs and their evidence

### Media classification

The historical native `ProcessRequests` handler did not implement the original
media classifier. It ignored the caller's guest structure and left `v0` unchanged.
The focused regression linked the local generated classifier and its original
caller, without copying or editing generated game code. The original baseline
passed; the native caller failed because residual `v0=2` rejected an otherwise
accepted disc, and the direct test found unchanged output fields.

The entry is now `DEFERRED`. DiskReady **2** remains the required availability
value; classification **0** is the original caller's media-acceptance route. Tests
cover incoming `v0` 0/1/2, output guards, callee-saved registers, guest stack,
legitimate rejection, bounded retry, and suspension/resumption through real
scheduler semaphores in controlled external dependencies.

### Mode-to-media-mask mapping

Deferring the classifier alone did not fix boot: a 30-second run still took the
original rejection path. Bounded tracing showed the resumed classifier returning
3. The next demonstrated divergence was the historical native Init: its original
maps the requested mode to an accepted-media mask, whereas the native handler
returned `CdvdModel::Reset()`'s zero. The original caller stores that return as its
mask, making subsequent checks reject valid matching media.

A regression using an invented lookup table passed with the original initializer
and lookup, then failed with native Init. Init is now also `DEFERRED`; no accepted
mask is invented. Real tracing subsequently confirmed original mapping,
classification, and caller acceptance, followed by successful disc reads.

### SIF completion must not invent guest requests

The local runtime's legacy SIF service treated a KCET request ring as a completion
ring: completing an asynchronous RPC created another guest request and signaled
the request-available semaphore. Original guest enqueue/consumer behavior showed
that ownership was wrong. A regression through actual `sceSifSetDma`, not merely
`SifCdvdServer::invoke`, copied exact synthetic sector bytes successfully and then
failed because unrelated guest request memory was modified.

`cmake/Aoe2Sif.cmake` now derives a runtime source under the build directory without
that verified legacy injection. Dependency sources remain unchanged. The removed
block is hash-guarded; unknown changes fail configuration, while an injection-free
upstream source is left alone. RPC replies and guest callbacks still complete
normally. Tests cover zero, one, and four occupied request entries, exact read
bytes, completion replies, and no extra request signals. The raw sector reader,
its error-reporting behavior, and mapped-file padding are not changed here.

### SCmd completion belongs to the worker, not its getter

The prior SCmd hook's synchronous-model assumption did not describe the real guest
queue. Even after media and SIF ownership repairs, native-on boot repeated its
first read. A diagnostic native-off comparison acknowledged successive transfers
and cleared the loading text. A focused regression then linked the original SCmd
getter: the original waited for pending work, but the native getter immediately
signaled completion before the scheduled producer ran.

The premature-signal override is removed. The original getter remains registered
with `cdvd=on`, `file_cd=on`, and both off. Two consecutive real scheduler waits
remain blocked until actual controlled completion. Existing completion tests were
retained and corrected to require an explicit completion producer. The original
SCmd worker/callback path, rather than another shortcut, releases waiters. The
NCmd getter hook and valid bit helpers are unchanged. The final native-on run now
matches the diagnostic native-off progress beyond the loading text.

## Isolated read integrity

`CdvdModel` is not fed by the real guest SIF request queue. Its tests use four
synthetic patterned sectors, not the owned ISO. They cover exact single/multiple
and final-sector bytes, address zero and the end of EE RAM; missing, nonregular
and unopenable images; EOF crossings and post-queue truncation; signed byte-count
overflow and invalid guest ranges; zero-sector no-ops; busy requests; explicit
retry/reset and repeated processing.

I/O failures leave destination and pending status unchanged. Data is staged before
writing guest RAM or publishing completion. These guarantees do not establish
identical failure behavior in the separate runtime SIF reader.

## Verification and reproduction

- Focused runtime CTest: **42/42 passed**, including `cdvd_media`, `cdvd_model`,
  `cdvd_rpc`, `native_file_adapters`, `overlay_dispatch`, and scheduler/DECI2 tests.
- Lightweight CTest: **37/37 passed**.
- Complete rebuilt diagnostic desktop/probe CTest in the capped Docker container:
  **40/40 passed**, including ELF/table initialization and decompiled-overlay tests.
- Independent final review found no correctness issues and reran `cdvd_media`,
  `cdvd_model`, and `cdvd_rpc`: **3/3 passed**. The reviewer also inspected the final
  black-screen-with-cursor screenshot.
- Python: **330 tests, OK, 4 skipped**. Skips are local HD install/interface assets,
  adjacent decompilation coverage, and the separate host probe build graph. They
  are not counted as coverage evidence.
- Diagnostic builds used `python3 tools/docker_build.py --platform desktop
  --diagnostics --jobs 4` on `docker-dev`, with compilation and linking successful.
- The final native-on run used `python3 tools/validate_desktop.py --run-seconds 30
  --interval 10`; its visual verdict remains **FAIL**, as reported above.
- One 120-second extension after the media-mask fix was justified by actual new
  disc progress. It still repeated reads and motivated the bounded completion
  investigation; no repeated long-run bypass experiments were used.

Configure the focused original-code tests with
`-DAOE2_BUILD_DECI2_RUNTIME_TESTS=ON -DAOE2_BUILD_CDVD_RUNTIME_TESTS=ON` in a separate
build directory. The latter is off by default. Enabling it without any required
owned generated unit fails configuration explicitly. It compiles only five
original units (classifier, caller, mode mapping, lookup, SCmd getter), not the
complete generated game table. The successful model and RPC fixtures require no
owned disc image. The overlay sentinel tests establish registration/dispatch
contracts only, not actual decompiled SMENU initialization or rendering.

## Local evidence (ignored, relative to the implementation worktree)

- Original regressions: `build/cdvd-media-diagnosis/media-red.log`, `direct-red.log`,
  `read-red.log`, `init-red.log`, `rpc-red.log`, `scmd-pending-red.log`.
- Source-bridge guard: `build/cdvd-media-diagnosis/sif-guard-red.log`,
  `sif-generic-red.log`, `sif-guard-green.log`.
- Suites: `build/cdvd-media-diagnosis/scmd-green.log`,
  `final-lightweight-tests.log`, `final-python-tests.log`,
  `final-desktop-probe-suite.log`.
- Final build/verdict/milestones: `build/cdvd-media-diagnosis/scmd-docker-build.log`,
  `scmd-validation.log`, `scmd-milestones.log`, and `build/desktop-validation/run.log`.
- Final screenshots: `.playwright-mcp/desktop-validation-010s.png`,
  `desktop-validation-020s.png`, `desktop-validation-030s.png`.
- Earlier runs are archived under `build/cdvd-media-diagnosis/baseline/`,
  `integration-only/`, `classifier-only-final/`, `init-only-30s/`,
  `extended-before-rpc-fix/`, `rpc-only-30s/`, and `original-cdvd-30s/`.
  Corresponding screenshot directories are under `.playwright-mcp/`.

No game binaries, generated game code, disc buffers, or diagnostic logs are added
to tracked files. The user-owned inputs and unrelated local changes are preserved.
