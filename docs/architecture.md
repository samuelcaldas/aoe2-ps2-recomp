# Architecture and Technical Design: AoE2 PS2 Recompilation

## 1. System Overview

This project is a native recompilation port of **Age of Empires II: The Age of Kings** (PlayStation 2 PAL edition, serial `SLES-50282`), targeting modern platforms without emulating an entire PS2 hardware stack in software at runtime.

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
  * Guest memory abstraction (`GuestMemory`: 32 MB PS2 EE main RAM layout).
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
3. **Switch as a Downstream Target**: Homebrew targets (Nintendo Switch via libnx/deko3d) require cross-compilation toolchains (devkitPro), specialized deployment setups, and hardware/emulator debugging. Attempting a Switch target before stabilizing runtime abstractions on desktop results in compounded failure modes.

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

## 6. Input Mapping and Future Switch Behavior

Age of Empires II is fundamentally designed for pointer-driven real-time strategy. The project decouples input through the `aoe2::input::ActionType` model:

* **Desktop Host**:
  * Mouse movement and clicks map directly to `PointerMove`, `PointerDown`, `PointerUp`, `PrimaryClick`, and `SecondaryClick`.
  * Keyboard navigation and scroll wheel map to `Pan`, `Zoom`, `MenuAccept`, `MenuBack`, and `Pause`.

* **Nintendo Switch Homebrew (Future Architecture)**:
  * **Handheld Mode**:
    * Direct touchscreen interaction: tapping and dragging on the capacitive touch screen emit native `PointerDown`, `PointerMove`, `PointerUp`, and `PrimaryClick` events, providing an intuitive RTS control experience.
    * Multi-touch gestures translate to `Pan` (two-finger scroll) and `Zoom` (pinch).
  * **Docked Mode**:
    * Full physical game controller operation: Left Stick controls an accelerated virtual cursor; Right Stick controls map panning; face buttons (A/B) handle `MenuAccept`/`MenuBack` and selection actions; triggers handle quick commands.
    * Optional USB mouse and keyboard support enabled via libnx HID services when connected to the dock.
