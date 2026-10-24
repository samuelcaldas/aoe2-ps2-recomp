# Pad: what the original libpad path does and what the native device must report

Evidence from the unmodified diagnostics build (desktop, Xvfb, 90 s, no native modules): the call trace
of the KCET pad module and of the Sony pad calls it makes, with the value returned on exit
(`[call-trace] ret ...`). Decomp function names are guest addresses. No binary snippets here.

## Observed

- **State query (`func_0010D2E0`, called from the module's per-slot poll `func_001275A0`):** returns `5`
  (executing a command) in every logged call, for both ports, 256 or more calls. It never leaves 5. The
  guest Sony library waits for an answer from the IOP pad service, which does not exist here, so the
  command never completes.
- **Port setup:** the module's init runs once; the Sony init, the slot-maximum query (ports 0 and 1) and the
  port open (ports 0 and 1, slot 0) are each called, and the module then proceeds to poll. Their return
  values are not in the trace (the calls block on the IOP round trip, so no exit line is written). The
  control flow after them (init result compared with 1, polling only after a successful open) shows that
  init and open returned 1.
- **Calls that never happen:** read, request state, actuator and mode queries, main-mode set, direct actuator
  set, actuator alignment, press-mode query, port close. The state machine never reaches data reading.
- **What the per-slot poll does with state 5:** it calls `func_001279B8`, which only resets the slot's
  buttons and counters. So the original, in this environment, clears the buttons every frame forever.

## The decomp disagrees with the binary here

The decomp's `func_001275A0` maps state 5 to `func_00127BD8` (error count), 6 to `func_001279B8` (reset) and 7
to `func_001279C0` (data processing). The generated code of the same function (the binary is the final
authority) maps them as follows, which is also what the public libpad numbering suggests:

| state | meaning (libpad) | function called |
|---|---|---|
| 0, 3, 4 | disconnected / other | `func_001279A0` (full reset) |
| 1 | pad found | `func_001279A8` (reset buttons) |
| 2 | controller type check | `func_001279B0` (process, flag 0) |
| 5 | executing a command | `func_001279B8` (reset buttons) |
| 6 | stable | `func_001279C0` (process, flag 1: reads the report) |
| 7 | error | `func_00127BD8` (count the error) |
| 0x63 | closed | `func_001279A0`, then the poll reports done |

Consequences: (1) the native port follows this table, not the decomp's; (2) the decomp's three branches are
swapped and must be reported to the decomp owner; (3) a device that reports 6 (stable) after a port is
opened makes the data path run, which settles the open question of the plan.

## Decisions for the native device

- Opened and connected port: state 6. Disconnected or never opened: 0. Closed: 0x63 is not produced.
- Commands (main-mode set, actuator set) complete immediately; request state is always 0 (complete).
- The read returns the number of bytes of the report for the current mode (4, 8 or 20); the module rejects
  anything outside 1..31, so 32 is never returned.
- Unknown: the meaning of the Sony call made right after init (`func_0010DEC0`, argument 0). The device returns
  1 and logs it; revisit when the module's behaviour shows what it needs.

## Native `pad_device` execution (Task 5)

Handler contract taken from `samuelcaldas/age-of-empires-2-decomp`, `src/kcet/kt_libpad.c` and
`include/kt_libpad.h` (clean working tree at the time). The 14 Sony pad entry points were identified by their
call sites in `kt_libpad.c` and the arguments passed: init and start (`func_00125C58`), slot maximum
(`func_00126040`), port open (`func_00127488`), port close (`func_00127750`), state (`func_001275A0`), read
(`func_00127E68`), request state, actuator info (`func_00127820`), mode info (`func_00127900`), main mode,
direct actuator set (`func_00128390`), actuator alignment, and the pressure-mode query.

**Run 1** (headless, real gamepad backend, no gamepad attached; 90 s): the device reported state 0 for the
port in every one of 550 polls, because the host reports no controller. The generated module therefore kept
resetting, exactly as it must for a disconnected pad. This says nothing about the stable-state hypothesis; it
shows the device is faithful when nothing is connected.

**Run 2** (same, with the diagnostics-only synthetic backend, `AOE2_PAD_SYNTHETIC=1`: port 0 connected,
Cross toggling every 500 ms): the hypothesis holds.

| observation | original (no native pad) | native `pad_device` |
|---|---|---|
| state returned | 5 forever | 6 for port 0, 0 for port 1 (no pad) |
| per-state function | `func_001279B8` (reset), 256+ | `func_001279C0` (process, flag 1), 256+ |
| read called | never | 256+, returns 8 (analog report) |
| report processing, buttons update | never | `func_00127E68`, `func_00127C88`: 256+ |
| actuator update | never | `func_00128390`: 256+ |
| init, slot max, open | returned 1 (inferred) | 1, 1 for both ports, 1 for both ports |

The mode-info call answers 0x41 for table entry 0 (digital) and the module then selects the analog mode; the
pressure-mode query returns 1. Rumble was not exercised: the game did not request any actuator output in 90 s
(no `[synthetic-pad] rumble` line), so the vibration path is verified only by unit tests so far.

The validator still ends `stalled_in_loop` (the game's own stall is a separate problem; this module neither
fixes nor worsens it in this window).

## Where the decomp of `kt_libpad` disagrees with the binary

Found while preparing the native port by comparing the decomp with the generated code of the same functions
(the binary is the authority). Each item needs a fix in the decomp before that function is ported from it.

1. **`func_001275A0` (per-state poll).** The decomp maps state 5 to `func_00127BD8`, 6 to `func_001279B8` and
   7 to `func_001279C0`. The binary maps 5 to `func_001279B8`, 6 to `func_001279C0` and 7 to `func_00127BD8`
   (table above).
2. **`func_001273D8` (per-slot step).** The decomp always advances the step and resets the state. The binary
   advances the step (wrapping at 3) and resets the state only when the step function returns non-zero; the
   flags bit `0x40000` is cleared before the call either way. The step table in the ELF is
   `[func_00127488 (open port), func_001275A0 (poll state), func_00127750 (close port)]`, so a pad that is open
   and polling stays on step 1.
3. **`func_00127820` (actuator descriptors).** The call that reads one actuator descriptor byte is
   `(port, slot, actuator, term)` with four arguments; the decomp shows a fifth, spurious `-1`.
4. **`func_001285F8` / `func_00127900` (mode info).** The decomp lost an argument: the count call passes the
   mode-table selector (4) and leaves the offset register as the caller left it; the library meaning is
   offset `-1` for the count. The mode-id loop leaves the offset unset (ignored for selectors 1 to 3).
5. **Not in the decomp at all:** `func_00125A70` (slot address by index), `func_00125AA8` and `func_00125AC8`
   (the semaphore lock and unlock) and `func_00125AE8`; they sit inside the module range but have no
   definition in `kt_libpad.c`.

Consequence for the plan: the native device (Tasks 3 to 5) is unaffected and already works. Tasks 6 to 8 (the
port of the module itself) cannot rely on the decomp for control flow in the branch-heavy functions
(`func_00127488`, `func_001276B8`, `func_00127750`, `func_001279C8`, `func_00127E68`, `func_00128390`,
`func_001281C0`, `func_001286A8`, `func_00128878`, `func_00126C30`) until each is checked against the binary.
