# EE game TTY

The project provides a minimal DECI2 host for the EE TTY channel. This is a
text diagnostic transport, not an interactive game console or a remote debugger.
Its output is distinct from the IOP emulator's logs and from runtime diagnostic
instrumentation. Capturing an assert does not fix the assert or prove gameplay.

## Protocol contract

The Open argument block contains protocol, callback option, handler, and the
SDK's kernel work area. The handler receives `(event, param, option)`; the work
area is **not** its third argument. The host does not inspect private game or
SDK work structures to find text.

ReqSend requests a transfer to the host. Poll dispatches a WRITE event on the EE
executor. The guest handler supplies data through ExSend. Once the framed packet
has been captured and the WRITE handler has returned, a later Poll delivers
WRITE_DONE. A handler that supplies no bytes is reported as an error, not polled
indefinitely. A transfer failure stops the EE on the next Poll; the runner's
output verification then exits with failure rather than inventing an unverified
error-callback contract or reporting success. Pending events are invalidated by
close/reset. Unsupported operations, protocols, destinations, and invalid guest
ranges fail explicitly.

TTY packets contain an eight-byte DECI2 header followed by four bytes of TTY
metadata; only the remaining payload is printed. The transport accepts split
headers and payloads, enforces the declared 16-bit packet length, and preserves
payload bytes. The terminal sink escapes control characters except LF and TAB,
leaving printable UTF-8 bytes intact. There is no 512-byte log truncation or
fixed message-count suppression.

Each runtime is explicitly bound to a scoped session. Callback continuations use
the existing scheduler's `GuestInvocation`, not a host-thread call into guest code.
No transport locks are held across a guest callback. The project runner closes
the session and flushes output before the upstream-style `_Exit` shutdown.

## Build and use

`AOE2_GAME_TTY` defaults to `ON`, independently of runtime tracing, `_DEBUG`, or
`RUNTIME_DECI2CALL`. With it disabled, Open reports no attached host, letting
software that supports that state disable its diagnostics safely.

The default sink writes `[game-tty]` followed by the escaped payload to stderr,
which is already captured by `tools/validate_desktop.py` in the ignored
`build/desktop-validation/run.log`. Payloads without newline are flushed too.
No additional log file or asset is tracked.

The runtime integration replaces exactly one upstream DECI2 syscall source in
`ps2_runtime`. Configuration fails if that source is missing or duplicated.
The pinned SDK checkout itself is not modified. The runner entry is maintained
in `platform/common/` to bind transport lifetime explicitly.

## Tests

The default lightweight CMake build includes `deci2_host`, which requires neither
owned game files nor upstream runtime libraries. Opt in to the synthetic scheduler
tests with `-DAOE2_BUILD_DECI2_RUNTIME_TESTS=ON`; this does not require generated
C++ or a game ELF. Run `ctest -R deci2 --output-on-failure` in isolated Release and
Debug builds, also covering `-DAOE2_GAME_TTY=OFF`. Build runtime tests inside the
desktop Docker image with the project's CPU/memory limits.

Local verification also executes the owned, recompiled EE TTY handler and its
SDK wrappers with a test-supplied packet: capture occurs once, the guest's send
flag clears, and completion takes two Poll calls. The address-specific harness
and its logs remain ignored under `build/tty-owned-handler/`; this isolated
handler check is not a full game boot.

A real-game check must identify text delivered through ExSend, not merely an
existing assert-hook message or IOP printf. A stalled/aborted run remains a
validation failure even if its diagnostic text is now visible. Local dependency
modifications must be reported; they are not evidence for the pinned revision.

## Validation record (2026-10-21)

- Main regression suites: 174 Python tests (one skip) and four CTest targets pass.
- Clean pinned runtime: 12 DECI2 tests pass across Release/Debug and TTY ON/OFF;
  the owned-handler check captures one packet and clears the send flag in two
  polls. Transport tests also pass AddressSanitizer, UndefinedBehaviorSanitizer,
  and leak detection.
- Full desktop run with the **local, uncommitted SIF runtime changes**: build
  succeeds; the 60-second run records **28,890 `[game-tty]` lines**, zero missing
  branch targets, and two real but unchanged frames. The loading screen remains
  stalled, so gameplay validation is **FAIL**. This is real EE TTY evidence, not
  a claim that the pinned upstream runtime boots the game.
- The separate full-game rebuild against the clean pin was cancelled after the
  local-fork run established end-to-end capture. Clean-pin claims above cover
  runtime/component and owned-handler tests, **not** a completed full-game build
  or boot in that configuration. No SDK working-tree changes were made here.

Local logs and verification metadata are retained in ignored
`build/tty-validation/`; address-specific handler evidence remains in ignored
`build/tty-owned-handler/`.

## Sources and licensing

ABI references, consulted 2026-10-21:

- [PS2SDK DECI2 declarations](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/kernel/include/deci2.h)
  and [syscall argument wrappers](https://github.com/ps2dev/ps2sdk/blob/329e2bb43287e16af16c4dde1c614e2662c24468/ee/kernel/src/deci2.c).
  PS2SDK is AFL-2.0; these are API references, not copied implementation.
- [PCSX2 DECI2 packet declarations](https://github.com/PCSX2/pcsx2/blob/1f4dd94db0633679e49bdec7faa0dd5246123ad0/pcsx2/RDebug/deci2.h)
  and [TTY framing](https://github.com/PCSX2/pcsx2/blob/1f4dd94db0633679e49bdec7faa0dd5246123ad0/pcsx2/RDebug/deci2_ttyp.cpp),
  GPL-3.0+. Used to establish generic framing; channel selection was also checked
  against the owned local ELF, whose evidence is not tracked.
- Pinned PS2Recomp, GPL-3.0: runtime callback conventions and runner lifecycle.
  Project integration linking that runtime inherits its GPL obligations.
