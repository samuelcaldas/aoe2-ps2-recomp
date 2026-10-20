# ADR 0001: Replace PS2 hardware at the KCET boundary

- Status: Accepted
- Date: 2026-10-20

## Context

The PS2 port of Age of Empires II puts a hardware layer from Konami Computer Entertainment Tokyo (KCET) between the PC-derived game code and the Sony libraries and hardware. Almost all contact between the game and the console goes through the KCET modules, which the owner's matching decompilation (`samuelcaldas/age-of-empires-2-decomp`, `docs/kcet_hardware_analysis.md`) identifies by their source-file assert strings:

| KCET module | Responsibility |
| :--- | :--- |
| `kt_mouse` | USB mouse: per-port work buffers fed with raw USB mouse packets (at most 8 bytes: buttons, X/Y deltas, wheel) |
| `kt_keybd`, `kt_usbkeybd_drv` | USB keyboard: key status per port, plus low-level requests such as caps-lock state |
| `kt_libpad` | DualShock 2 on top of Sony libpad |
| `kt_libcdvd_server` | CD/DVD RPC server |
| `kt_file` | File I/O abstraction by file kind |
| `kt_mc_2` | Memory card |
| `kt_gpu` | Graphics Synthesizer dispatch through a function table |
| `kt_kernel` | Threads and semaphores |
| `kt_hd` | Sony development-kit filesystem (`host0:`, `sim:`, `atfile:`); not used by the retail disc |

The runtime can already replace any recompiled guest function with a host implementation (`PS2Runtime::replaceFunction`, applied through the game-override mechanism), and the generated code has a uniform calling convention, so a KCET function can be swapped without touching generated C++.

The alternative, emulating the lower layers (the USB stack on the IOP, SIF RPC transport, the GS), is what §5 and §6 of `docs/architecture.md` already reject.

## Decision

High-level replacements hook the KCET boundary: a KCET function is replaced by a host implementation with the same guest-visible effect, and the code below it (Sony libraries, IOP drivers, hardware) is not emulated. The rest of the game, including its own cursor, menus, and input handling, keeps running as recompiled code.

| KCET module | Replacement | Host source |
| :--- | :--- | :--- |
| `kt_mouse` | Write host USB mouse reports into the KCET port buffers in the same packet format, so the game's own cursor logic runs unchanged | `UsbMouseTranslator` / raw relative reports; touch converted to relative motion |
| `kt_keybd`, `kt_usbkeybd_drv` | Answer key-status queries from `GuestKeyboardAdapter` boot-protocol reports | Host keyboard (desktop, Android, Switch docked USB) |
| `kt_libpad` | None needed: the PS2Recomp runtime already emulates libpad from raylib gamepads and keyboard | Gamepads, Joy-Cons, Pro Controller |
| `kt_libcdvd_server`, `kt_file` | None needed while the runtime's CD HLE serves the extracted disc; hook only if a blocker appears | Extracted disc (`tools/import_owned_disc.py`) |
| `kt_mc_2` | None needed while the runtime's `mcserv` HLE works | Host memory-card directory |
| `kt_gpu` | Seam for the high-level `IRenderer` (sprites, tiles, UI) when the GS path is replaced | OpenGL ES 3 backend (`docs/sot/prior-art-and-reuse.md` §3) |
| `kt_kernel` | None: the runtime's EE scheduler provides threads and semaphores | — |
| `kt_hd` | Not replaced; unused on the retail disc | — |

Order of work: input (`kt_mouse`, `kt_keybd`) first, because joystick, keyboard, and mouse are required on every platform; then `kt_gpu` with the renderer; the others only if they block.

## Rules

- **Decompilation as reference (authorized by the owner, 2026-10-20).** The owner's matching decompilation may be used as the reference for every KCET replacement: its matched C/C++ defines each function's contract (buffer layouts, packet formats, return values, assertions), and matched code may be ported into the host-side replacement when that is the simplest faithful implementation. The decompilation repository is not modified from here.

- **Addresses stay out of tracked files.** Hooks are written against KCET semantics. Their guest addresses are resolved at build or run time from the owner's decompilation symbols into ignored local files (for example under `game/` or `analysis/generated/`), never hardcoded in tracked code or docs.
- **One replacement per KCET function.** A hook replaces a whole KCET function, never a fragment of generated code, and never edits generated C++ or adds TOML `stubs`/`skip`.
- **No fabricated results.** A hook must produce what the original would produce for the same device state (for example "no mouse connected" when the host has none). Placeholder `ret0`/`ret1` behavior is not allowed.
- **Same semantics on every platform.** Host input reaches the hooks through the platform-neutral `input/` layer (and raylib on desktop, Android, and Switch), so the guest sees identical USB packets everywhere.
- **Verified against the decompilation.** Each replaced function must be matched (100%) in the decompilation first, so its contract (buffer layout, return values, assertions) is known rather than guessed.

## Consequences

- Mouse and keyboard work identically on desktop, Android, and Switch without USB/IOP emulation, and the game keeps its original PS2 cursor behavior.
- Each hook depends on a matched decompilation function; today only `kt_mouse` has fully matched functions, so `kt_keybd` and `kt_gpu` need matching work first.
- The address-resolution step is a new local artifact that `tools/` must generate and verify, in the same way the function map is verified.
