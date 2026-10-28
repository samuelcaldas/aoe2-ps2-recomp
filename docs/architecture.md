# Architecture and Technical Design: AoE2 PS2 Recompilation

## 1. System Overview

This project is a native recompilation port of **Age of Empires II: The Age of Kings** (PlayStation 2 PAL edition, serial `SLES-50282`), targeting modern platforms without emulating an entire PS2 hardware stack in software at runtime.

**Primary target: Nintendo Switch (homebrew, libnx + deko3d).** The shipped product is the Switch build. Desktop x86-64/Linux is the host where the runtime is developed, debugged, and validated first; ARM64 is the portability gate in between (§3). The work order does not change.

The pipeline architecture proceeds as follows:

```
game/SLES-50282/AOK.ELF (User-provided, validated SHA-1)
       │
       ▼ (Ghidra + PS2Recomp static analysis & translation)
PS2Recomp-generated C++ (untracked, generated locally)
       │
       ▼
Host runtime & platform abstractions
       │
       ├──> Desktop Executable (x86-64 / Linux first)
       ├──> ARM64 Portability Gate (macOS / Linux aarch64)
       └──> Nintendo Switch Homebrew Backend (libnx + deko3d later)
```

---

## 2. Layer Boundaries and Modular Responsibilities

To ensure separation of concerns and portability across systems, the project is organized into strictly isolated layers:

* **`core/`**:
  * Target metadata specification (serial, expected ELF hash, title, region).
  * Verification rules and invariants.
  * Deterministic runtime-neutral types, endian-safe accessors, and fundamental data structures.
  * Zero external host OS dependencies.

* **`runtime/`**:
  * Guest memory abstraction (`GuestMemory`: 64 MiB EE RAM in the dedicated fork, expanded from the PS2's original 32 MiB). Runtime heap/HLE/stack reservations are documented in the [dependency inventory](runtime-dependency-inventory.md#5-memory-and-presentation-seams-expanded-to-64-mb).
  * Function dispatch adapters and recompiled symbol binding stubs.
  * Abstract platform services interface (`PlatformServices`) for clocks, logging, and OS interactions.

* **`platform/`**:
  * Concrete host implementations of platform services.
  * `platform/desktop/`: Initial Linux / x86-64 host layer handling windowing, standard desktop timing, and input recording.
  * Future `platform/switch/`: Switch homebrew implementation interfacing with Horizon OS.

* **`input/`**:
  * Platform-neutral action definitions:
    * Pointer actions: `PointerDown`, `PointerMove`, `PointerUp`, `PrimaryClick`, `SecondaryClick`, `Pan`, `Zoom`.
    * Menu/Game actions: `MenuAccept`, `MenuBack`, `Pause`.
  * `InputSink` interface abstracting event emission from event handling.
  * `TouchGestureTranslator` + `SurfaceMapping`: touchscreen contacts → pointer actions in guest framebuffer coordinates.
  * `GuestMouseAdapter`: folds pointer actions into the per-frame USB-mouse state the recompiled game polls (see §6).

* **`renderer/`**:
  * Abstract graphics interface (`IRenderer`: `Initialize`, `BeginFrame`, `EndFrame`, `Shutdown`).
  * `NullRenderer`: Capture/headless test backend used for validation and CI without GPU dependencies.
  * No GS emulation code in this layer.

* **`tools/`**:
  * Host-side automation scripts (Python 3 stdlib):
    * `tools/import_owned_game.py`: Fail-fast, checksum-verified ELF import.
    * `tools/verify_owned_game.py`: Verification of local game asset integrity.
    * `tools/bootstrap_dependencies.py`: Third-party git repository management and lockfile maintenance.
    * `tools/validate_ghidra_env.py`: Environment validation for the external Ghidra prerequisite.

* **`tests/`**:
  * Focused automated unit and integration tests (Python `unittest` and C++ smoke test via `ctest`).

---

## 3. Why Desktop x86-64 Precedes ARM64 and Switch

Developing a native recompilation requires rapid iterative debugging: inspecting register dumps, validating memory layouts, tracing instruction flow, and analyzing crashes.

1. **Development Velocity**: Desktop Linux x86-64 provides immediate compilation, standard debuggers (`gdb`, `lldb`, `valgrind`, AddressSanitizer, UBSan), and instantaneous iteration cycles.
2. **Deterministic Baseline**: Once the game loops and memory behavior are proven sound and deterministic on desktop x86-64, porting to ARM64 validates endianness assumptions, alignment constraints, and instruction behavior on weakly ordered memory architectures.
3. **Switch as the Primary Target, Reached Last**: Nintendo Switch homebrew (libnx/deko3d) is the shipping target, but it requires cross-compilation toolchains (devkitPro), specialized deployment setups, and hardware/emulator debugging. Attempting it before stabilizing runtime abstractions on desktop results in compounded failure modes, so it is built after the desktop and ARM64 gates pass.

---

## 4. PS2 Emotion Engine (EE): Why It Exceeds Plain MIPS

The PlayStation 2 Emotion Engine is not an off-the-shelf MIPS processor. It is a highly specialized heterogeneous multiprocessing system consisting of:

* **MIPS R5900 Core**:
  * 64-bit MIPS III / IV instruction set architecture with custom 128-bit multimedia extensions (MMI).
  * 128-bit wide general-purpose registers (GPRs) capable of parallel SIMD operations (e.g., parallel add/subtract/multiply on dual 64-bit or quad 32-bit values).
  * Non-standard branch delay slot semantics and specific 128-bit pipeline alignment behaviors.
* **Vector Units (VU0 & VU1)**:
  * Autonomous VLIW vector coprocessors operating in parallel with the main CPU.
  * VU0 can operate in macro mode (coprocessor instructions directly in the EE instruction stream) or micro mode (independent microcode program).
  * VU1 is a dedicated geometry processor tightly coupled to the Graphics Synthesizer via the GIF interface.
* **Graphics Synthesizer (GS)**:
  * A rasterizer with embedded 4 MB of ultra-wide eDRAM (up to 48 GB/s bandwidth), driven by privileged control packets rather than standard high-level command buffers.
* **Direct Memory Access Controller (DMAC)**:
  * 10 independent DMA channels mediating transfers between EE, VUs, GS, and IOP without CPU intervention.
* **Input/Output Processor (IOP)**:
  * A dedicated sub-system (MIPS R3000A core) managing peripherals, audio (SPU2), CD/DVD optical drive, and memory cards, communicating with the EE via SIF (Sub-system Interface).

Due to this architecture, naive instruction-level recompilation is insufficient without accurate emulation or high-level replacement of co-processors and hardware buses.

---

## 5. High-Level Rendering Replacement Strategy

A key architectural mandate of this project is:
> **Rendering is to be replaced at a high level where feasible, rather than attempting full low-level Graphics Synthesizer (GS) emulation first.**

* **Rationale**: Complete GS emulation requires reproducing eDRAM addressing, complex pixel pipeline alpha tests, bit-exact blending modes, and micro-packet GIF parsing. For Age of Empires II (a 2D isometric RTS that was originally written for PC Win32 DirectX/GDI and later adapted to PS2), game rendering operates over discrete sprite blits, tile planes, fonts, and 2D UI elements.
* **Approach**:
  1. Identify high-level engine draw commands, sprite blit loops, and tile renderers in the recompiled code.
  2. Map these operations directly to modern renderers (SDL2/Vulkan on desktop, deko3d on Nintendo Switch) via `IRenderer`.
  3. Bypassing low-level GS register emulation eliminates extreme shader complexity and yields optimal performance and native high-resolution rendering.

---

## 6. Input: Replacing the PS2 USB Mouse with Switch Touch

### 6.1 Background

The PS2 release of Age of Empires II supports a **USB mouse** in addition to the DualShock 2. On real hardware the mouse is serviced by an IOP-side USB driver and its state reaches the EE over SIF; the game polls it and draws its own cursor. The Switch port **replaces that mouse with the Switch touchscreen**: touch drives the same pointer path the game already uses for the mouse, so menus, unit selection, and commands keep their original PS2 mouse behavior.

Consistent with §5, this is a **high-level replacement**: the runtime does not emulate USB HID, the IOP USB stack, or SIF transport for the mouse. Instead the guest's mouse-state read is serviced directly from `GuestMouseAdapter`. The exact guest seam (the SIF RPC to the IOP mouse module vs. the game's own input-poll wrapper) is not yet identified in the recompiled output; it must be located without adding speculative stubs.

### 6.2 Pipeline

```
Switch touchscreen (libnx HID, 1280x720)        Desktop mouse / touch (SDL)
            │ TouchContact                                │ ActionEvent
            ▼                                             │
  TouchGestureTranslator ── SurfaceMapping ──▶ ActionEvent (guest pixels)
                                                          │
                                                          ▼
                                                 GuestMouseAdapter
                                                          │ GuestMouseState per frame
                                                          ▼
                                         recompiled game's mouse poll
```

* **`SurfaceMapping`** maps host coordinates onto the guest logical framebuffer (640x448 logical extent, confirmed by the game's `RenderManager` and GS presentation calibration), assuming the image is aspect-fit and centered. On a 16:9 screen that maps with pillarbox bars; touches on the bars clamp to the nearest active edge.
* **`TouchGestureTranslator`** (platform-neutral, unit-tested on desktop) turns contacts into pointer actions:

  | Touch gesture | Action(s) | Guest mouse equivalent |
  | :--- | :--- | :--- |
  | Tap (< 500 ms, < 8 guest px movement) | `PointerMove` + `PrimaryClick` | Move cursor, left click |
  | Long press (≥ 500 ms, stationary) | `PointerMove` + `SecondaryClick` | Move cursor, right click |
  | One-finger drag | `PointerDown` → `PointerMove`… → `PointerUp` | Left-button drag (box select) |
  | Two-finger drag | `Pan` (delta in guest px) | None — routed to a high-level camera hook |

  Touch has no hover, so the cursor jumps to the contact point. Thresholds live in `GestureConfig`. Pinch is not mapped: the original game has no camera zoom.
* **`GuestMouseAdapter`** keeps the cursor position and button state and queues every button transition, so a tap that presses and releases within one host frame is still observed by the guest as one pressed poll followed by one released poll. `Pan`, `Zoom`, and menu actions do not touch the guest mouse.

### 6.3 Platform behavior

* **Switch handheld**: the touchscreen is the primary pointer, via the pipeline above. A future `platform/switch/` only converts libnx touch states into `TouchContact`s.
* **Switch docked** (no touchscreen): Left Stick drives an accelerated virtual cursor that emits the same pointer actions; Right Stick emits `Pan`; A/B map to `MenuAccept`/`MenuBack`. A USB mouse and keyboard plugged into the dock mirror the PS2's USB peripherals (`SourcesFor(OperationMode::Docked)` enables them; handheld uses touch + Joy-Cons):
  * **`UsbMouseTranslator`** takes libnx-style relative reports (`HostMouseReport`: motion in counts plus a button mask), keeps an absolute cursor clamped to the guest framebuffer (sensitivity in `MouseConfig`), and emits the same actions as touch: left button `PointerDown`/`PointerMove`/`PointerUp`, right button `SecondaryClick` on press. Its sink is normally `GuestMouseAdapter`. The wheel is not mapped.
  * **`GuestKeyboardAdapter`** produces USB HID boot-protocol reports (modifier bits for usages `0xE0`–`0xE7`, six key slots, `ErrorRollOver` beyond six) and queues transitions. `UsbKeyboardRpcModel` answers the five keyboard service requests and queues changed reports with a three-record bound; the resolver identifies its six guest hook roles from the owned ELF.
  * Mouse and keyboard handlers are separate modules applied by one KCET registry. Its host-tested parser accepts known mouse/keyboard roles and an allowlist of runtime pad aliases; malformed, duplicate, or unknown entries and failed replacements stop loading rather than silently omitting input. Addresses remain in the ignored local hook file only. Host tests and syntax checks pass; in-game keyboard input, pad alias resolution, and Switch input remain unvalidated. These hooks do not add an IOP USB stack.
* **Desktop host**: the host mouse emits pointer actions straight into `GuestMouseAdapter`; touch input can be exercised through the same translator for testing.

---

## 7. Widescreen (Future Option)

> **Status:** design note only. It is out of scope for the bootstrap phase (see README non-goals: no asset upscaling and no gameplay-visible changes). Nothing here is implemented.

### 7.1 How PC widescreen mods work (reference)

On PC, the community patch AoE2Wide patches the resolution constants (`1280`, `1024`, …) inside `age2_x1.exe` and rebuilds the fixed-size interface SLPs in `interfac.drs` at the new size. The engine is a 2D isometric renderer with no camera zoom. A wider viewport therefore shows more of the map instead of stretching it. What breaks at new resolutions is hard-coded constants and pre-sized interface bitmaps, not the world renderer. Age of Empires II (2013), formerly the HD Edition, supports arbitrary resolutions natively.

### 7.2 Widescreen playfield

Because rendering is replaced at a high level (§5), widescreen does not need GS changes:

1. Locate the guest's viewport and screen-size values in the recompiled code. These are the PS2 equivalent of the constants AoE2Wide patches: map view rectangle, scroll limits, visible-tile culling bounds, cursor clamp. Override them through runtime hooks, not by editing generated sources.
2. Widen the guest extent (e.g. 640x448 → 800x448 or dynamic 16:9) so the game simulates and culls a wider view, and `IRenderer` draws it at native host resolution.
3. Input keeps working unchanged: `SurfaceMapping` takes the guest extent as a parameter, so a 16:9 guest fills the 1280x720 Switch screen without pillarbox bars. `GuestMouseAdapter` clamps to the new extent.

Gameplay impact (more of the map visible) matches the PC widescreen mods. It must be an optional setting, and the default stays 4:3 for parity with the original PS2 game.

### 7.3 HD and widescreen assets from Age of Empires II (2013)

Age of Empires II (2013) is the primary target source of higher-resolution, widescreen-ready, and modern decoupled assets. Its openage converter ID is `HD`. Detailed architectural specification for decoupling and platform layout: see `docs/sot/hd-assets-strategy.md`.

Rationale:

* It is the closest data lineage to the original game: the same SLP sprite format, the same graphic/unit identifiers, and it is supported by the openage converter. That keeps a PS2 → PC asset correlation tractable. Definitive Edition (`DE2`) has redrawn art in a different format (SMX) and is a later, separate option.
* Its interface is built for arbitrary and widescreen resolutions, which gives a reference for a widescreen HUD instead of stretching 4:3 bitmaps the way AoE2Wide does.

Rules, following the same provenance model as `AOK.ELF` (phase 1 implementation: `docs/sot/hd-assets-strategy.md` §3):

* The user must own the 2013 edition. Its files are supplied and extracted locally, never committed or distributed. Place them under `game/` (already gitignored). Record only hashes, counts and versions in tracked metadata.
* Add an import/verify tool equivalent to `tools/import_owned_game.py` (fail-fast, checksum-verified), and an untracked asset-manifest entry.
* PS2 assets remain the baseline. The HD set is an override layer: the renderer resolves each guest asset reference through a correlation table (PS2 asset → 2013-edition asset). It falls back to the PS2 asset whenever no verified match exists.
* No code or data from unofficial disassembly projects (clean-room rule). Asset correlation must be derived from the user's own files.

### 7.4 Widescreen HUD

The PS2 HUD (top resource bar, bottom command and minimap panel) is laid out for a 4:3 screen. Options, in order of preference:

1. **Re-laid-out HUD.** The renderer anchors HUD panels to the screen edges and fills the extra width with panel graphics from the 2013 edition's widescreen interface.
2. **Centered 4:3 HUD.** The original HUD stays 4:3 and centered over a widescreen playfield, with filler art on the sides. This needs no new assets.
3. **Stretched HUD.** Not acceptable, because it distorts sprites and fonts.

**Input constraint:** the game's own logic still hit-tests HUD buttons in its original HUD coordinates. If the renderer moves HUD panels, the input path must apply the inverse transform: a HUD-layout mapping between `SurfaceMapping` and `GuestMouseAdapter` that maps a touch on a relocated panel back to that panel's original guest coordinates. Playfield touches map directly to the widened world view. Every relocated HUD region therefore needs a matching entry in both the render layout and the input layout.

### 7.5 Open questions

* Where the PS2 build stores its viewport, scroll-limit and culling constants, and whether all of them are runtime-overridable without touching generated code.
* How the PS2 sprite/graphic identifiers correspond to those in the 2013 edition, and how much of the PS2 interface differs from the PC interface.
* How the 2013 edition composes its HUD at arbitrary widths (tiled filler vs. per-resolution assets). This must be verified against the user's own installation before choosing HUD option 1.

## 8. KCET Boundary for High-Level Replacement

The PS2 port routes nearly all hardware access through KCET (Konami Computer Entertainment Tokyo) modules: mouse, keyboard, pad, CD/DVD, files, memory card, GS dispatch, network/multiplayer, and kernel objects. High-level replacements hook that boundary: a KCET function is replaced by a host implementation through the runtime's function-replacement mechanism, and nothing below it is emulated. Detailed evolution of video, audio, textures, and controls across the three game editions: see `docs/sot/subsystems-assets-evolution.md`. Mouse and keyboard come first (host reports written in the KCET packet format, so the game's own cursor logic runs unchanged); `kt_gpu` is the seam for the high-level renderer; `mydplay.cpp` is the seam for modern UDP LAN multiplayer (see `docs/sot/multiplayer-lan-architecture.md`). Guest addresses for hooks are resolved from the owner's decompilation into ignored local files, never tracked. Decision, module table, and rules: `docs/adr/0001-kcet-boundary-replacement.md`.
