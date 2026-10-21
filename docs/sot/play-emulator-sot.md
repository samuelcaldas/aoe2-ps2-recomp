# Play! Emulator as Source of Truth (SOT)

This document establishes the role of the **Play!** PlayStation 2 emulator (Jean-Philip Desjardins / jpd002, BSD 2-Clause, [purei.org](https://purei.org/), [GitHub: jpd002/Play-](https://github.com/jpd002/Play-)) as an authoritative runtime Source of Truth (SOT) for reference behavior, subsystem contracts, and diagnostic comparison in the native static recompilation of *Age of Empires II: The Age of Kings* (PS2 PAL `SLES-50282`).

---

## 1. Why Play! as SOT

In native static recompilation projects, when the recompiled guest stalls or exhibits undefined behavior, an emulator provides the ground truth for hardware, BIOS, and subsystem interactions.

Play! is uniquely suited as an architectural and behavioral reference because:
1. **High-Level Emulation (HLE) focus**: Play! uses extensive HLE for PS2 IOP modules, system calls, and BIOS routines. Rather than simulating every cycle of IOP hardware, it documents and implements high-level OS and RPC semantics.
2. **Clean C++ codebase**: Its modular design (`EE`, `GS`, `IOP`, `SIF`, `HLE`) makes it straightforward to read and verify hardware handshake sequences and register contracts.
3. **Multiplatform portability**: Play! runs across desktop Linux/Windows, macOS/iOS, and Android, matching our downstream target roadmap.
4. **Permissive BSD 2-Clause license**: Inspecting logic, algorithms, and register behaviors does not risk licensing encumbrance.

---

## 2. Primary Reference Areas

### 2.1 SIF Boot Handshake and MsCom (`0x80000002`)
As documented in [`kcet-stall-log.md`](file:///home/samuelcaldas/repos/aoe2-ps2-recomp/docs/sot/kcet-stall-log.md), our current diagnostic baseline stalls during the initial SIF boot handshake where the guest EE polls a memory reply word triggered by the MsCom register (`0x80000002`):
- **Play! Implementation**: Play!'s SIF and IOP HLE modules (`SIF.cpp`, `BiosDebug.cpp`, and IOP RPC models) define the exact handshake sequence, buffer layouts, packet formats, and reply flags that the guest expects.
- **Role**: SOT for how the IOP responds to initial boot DMA packets and signals completion to the EE without adding speculative or game-specific stubs.

### 2.2 KCET Hardware Abstraction Layer Boundaries
The game interfaces with the PS2 through Konami Computer Entertainment Tokyo (KCET) libraries (`kt_mouse`, `kt_keybd`, `kt_gpu`, file systems, sound, and memory cards):
- **Play! Reference**: Play! documents the standard Sony IOP RPC servers (`libpad`, `libmc`, `sifcmd`, `sifrpc`, `cdvd`) that KCET's low-level modules invoke under the hood.
- **Role**: Verifying packet layouts, command IDs, and expected return codes when hooking or replacing KCET boundary layers (ADR 0001).

### 2.3 Dynamic Overlay Loading (PT_LOAD Segments)
`AOK.ELF` features eight executable `PT_LOAD` segments with `filesz = 0` (see `desktop-runtime-probe.md` §7.1). The game streams code overlays dynamically from disc at runtime:
- **Play! Reference**: Observing disc sector reads, buffer destinations in EE memory, and overlay execution flow under Play!'s debugger or execution traces.
- **Role**: Identifying exact disc offsets, overlay file names, and memory entry points as executed in a compliant PS2 environment.

---

## 3. Operational Rules and Constraints

In accordance with our project provenance and clean-room policies (`docs/sot/prior-art-and-reuse.md`):

1. **Behavioral Reference, Not Code Copying**: Play! serves as an architectural and behavioral specification. PS2Recomp runtime code links against its pinned runtime; Play! logic is used to verify expected guest semantics and register protocols.
2. **No Secret or Proprietary Assets**: Play! tests must run against the user-owned `game/SLES-50282/AOK.ELF` and disc image (`ALL_DATA.BIN` / `.iso`), without proprietary BIOS dependencies wherever HLE is sufficient.
3. **No Addresses or Game Disassembly in Tracked Files**: Traces, memory dumps, and registers obtained from Play! debugging must adhere to the rule that tracked metadata holds only counts, versions, and digests.
