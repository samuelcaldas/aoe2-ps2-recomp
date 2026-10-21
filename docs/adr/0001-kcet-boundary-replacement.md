# ADR 0001: Replace PS2 hardware at the KCET boundary

- Status: Accepted
- Date: 2026-10-20

## Context

### The Three Independent Native Input Channels in AoE2 PS2
A common misconception is that console RTS ports merely emulate mouse movement with a joystick, or that keyboards are remapped to controller buttons. The PS2 edition of Age of Empires II implements **three separate, concurrent, fully native input channels** designed specifically for console play:

1. **Native Gamepad Subsystem (`kt_libpad` -> Game Engine):**
   The Konami/KCET development team engineered a comprehensive, native DualShock 2 interface directly into the simulation and tactical AI engine:
   - *Controller Action Engine* (`func_0032A060`): State machine executing contextual actions on units and buildings.
   - *Command Router & Order Dispatch* (`func_0032E220`, `func_0032E6B0`): Direct assignment of patrol, attack-move, formation, and stance orders mapped to face buttons and triggers.
   - *Analog Camera Navigation* (`func_0032BCE0`, `func_0032BE50`): Smooth velocity-based panning, acceleration curves, and coordinate clamping driven by the analog sticks.
   - *Box Selection & Target Queries* (`func_003299F0`, `func_0032D400`, `func_0032DA90`): Screen-space entity pick queries and area-drag selection tailored for stick and button precision.
   - *Controller Context & Pulse* (`func_0032BF70`, `func_0032C320`): Ring/radial menus, button hold/repeat timing, and HUD prompts.
   This channel reads DualShock 2 data via `kt_libpad` and `libpad.h`. Host gamepads (PC controllers, Switch Joy-Cons/Pro Controller, Android Bluetooth gamepads) feed directly into this native console subsystem 1:1 without synthetic translation.

2. **Native USB Mouse Subsystem (`kt_mouse` -> Game Engine):**
   The PS2 release natively supported a USB mouse (PS2 mouse protocol over SIF RPC `0x80000210`). The game engine contains its own authentic PS2 mouse pointer renderer, hover states, right-click command dispatch, and rectangular drag box. We hook `kt_mouse` so host mice (PC mouse, Switch docked USB mouse) and the Switch handheld touchscreen feed raw 8-byte USB packets directly into this pipeline.

3. **Native USB Keyboard Subsystem (`kt_keybd`, `kt_usbkeybd_drv` -> Game Engine):**
   The PS2 release natively supported a USB keyboard for hotkeys, cheat codes, chat, and text entry. We hook `kt_keybd` so host keyboards feed standard USB HID boot reports. (Keyboard-to-pad button emulation in `ps2_pad.cpp` is strictly an optional fallback for headless test or keyboard-only desktop play without a gamepad, not the architectural design).

## Context (KCET Hardware Layer)

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
| `kt_libpad` | DualShock 2 gamepad and analog cursor emulation mapped to host controllers | Host gamepads, Joy-Cons, Pro Controller via raylib / SDL |
| `kt_libcdvd_server`, `kt_file` | File access redirected to decoupled external assets or extracted disc files | External `assets/` folder via `LooseHdAssetProvider` |
| `kt_mc_2` | Save game operations redirected to host platform save files | Host memory-card / save directory |
| `kt_gpu` | Seam for the high-level `IRenderer` (sprites, tiles, UI) bypassing GS VRAM and CLUT swizzling | OpenGL ES 3 / Mesa / deko3d backend (`docs/sot/subsystems-assets-evolution.md`) |
| `mydplay.cpp` | DirectPlay Compatible Interface hook redirecting P2P lockstep to UDP LAN | `aoe2::net::UdpLanTransport` (`docs/sot/multiplayer-lan-architecture.md`) |
| `soundcd.irx` / audio | High-level sound effects and music streams bypassing SPU2 / IOP | Host audio backend (SDL2 / raylib / OpenAL) playing WAV/MP3 |
| `kt_kernel` | None: the runtime's EE scheduler provides threads and semaphores | — |
| `kt_hd` | Not replaced; unused on the retail disc | — |

Order of work: input (`kt_mouse`, `kt_keybd`) first; then `kt_gpu` with the renderer and `mydplay` with network; then sound overrides (`soundcd.irx`) and file subsystems.

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

## Implementation status

- `kt_mouse`: implemented. `tools/resolve_kcet_hooks.py` finds the RPC bind (USB mouse service SID `0x80000210`) and the three RPC wrappers (functions 1 info, 2 read events, 3 device info) in the decompilation by structure and writes their addresses to the ignored `game/SLES-50282/kcet_hooks.ini`. `game_hooks/src/kcet_mouse_overrides.cpp` registers a game override that replaces them with host functions answering from `aoe2::input::UsbMouseRpcModel`, sampled from the platform mouse through raylib once per game poll. Without the hooks file the bind loop has no IOP service to reach, so the file is required to boot past mouse initialization.
- `kt_keybd`: the host RPC model and ELF resolver for the six RPC roles are implemented. The game-side keyboard handlers and registry integration are not yet implemented, so keyboard input still relies on the runtime's pad emulation meanwhile. The host report payload models a boot keyboard report; the transferred metadata byte is approximated as zero because its application meaning is not established.
- `mydplay.cpp`: specified in `docs/sot/multiplayer-lan-architecture.md`. Intercepts the DirectPlay vtable at `0x338140` to route multiplayer lockstep over UDP LAN sockets.
- `kt_gpu`: specified in `docs/sot/subsystems-assets-evolution.md`. Seam for high-level rendering via `aoe2::renderer::IRenderer`, bypassing GS hardware CLUT swizzling and VRAM limitations.
- `soundcd.irx`: specified in `docs/sot/subsystems-assets-evolution.md`. Intercepts audio triggers to play WAV/MP3 files from host audio backends directly.
- The rest: not started.
