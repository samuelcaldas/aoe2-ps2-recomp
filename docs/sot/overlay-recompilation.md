# Overlay recompilation: loader findings and plan

Roles only; no guest addresses (provenance rules). Local mapping: `tools/map_overlays.py`.

## How the game loads an overlay (from the owned ELF)

- A per-screen state machine requests an overlay by file name and load address, then
  polls a loader until it reports done. The loader prefixes the language directory, reads the
  **whole file, header included,** to the load address through the disc stack, and invalidates caches.
  Nothing is relocated or decompressed by the loader.
- **IOP/SIF Channel (Thread 7 / FUN_0012B648):** The eight overlays (`MWo3` / `PT_LOAD` segments with
  `filesz = 0`: `SMENU_CD.BIN`, `DCOMP_CD.BIN`, etc.) pass through the exact same disc channel driven by
  Thread 7 (`func_0012B648`): they are stored on the CD-ROM and read by `kt_libcdvd_server` via
  SIF CDVDFSV commands (`sid=0x80000593`, `fn=0x22`). Consequently, the `cdvdman:22` (`sceCdRead0`)
  IOP fix resolves both general asset reading and loading the executable code overlays into memory
  (`0x01D98000` onward).
- The main code enters an overlay with plain `jal` to fixed addresses inside its load region
  (an init entry, then a per-frame entry for the lobby). The generated main-code C++ has no
  function there, so such a call is a missing function.
- The 0x40-byte file header holds the overlay id at offset 4. After a load, the id read from guest
  memory at the region base says which overlay occupies it. Six overlays share one base.
- BSS is not in the file; it follows the loaded bytes in memory.

## Consequences for recompilation

1. Each overlay is a raw EE image at a known base: import into Ghidra as raw binary, language
   `MIPS:LE:64:64-32addr`, base = load address, bytes = the whole file. Export a function map per overlay.
2. Generate C++ per overlay, in a separate namespace/output directory, never mixed with main code.
3. The pinned runtime cannot host them: one dense dispatch table over the main code range, and
   `registerFunction`/`replaceFunction` reject other addresses. Needed in a local patch series
   (applied by `bootstrap_dependencies.py`): a per-overlay table plus a lookup that, for addresses in an overlay
   region, selects the table by the id found in guest memory at that region's base.
4. Self-modifying risk: an overlay swapped while a previous one is executing would be a bug in the
   guest; the lookup must fail loudly (missing function) when the id matches no recompiled overlay.

## Order of work

1. Ghidra raw import + function export for one small overlay (the smallest with code) as proof.
2. Overlay-aware generation + manifest rules (hashes/counts only).
3. Runtime patch series and a host test with a synthetic two-overlay table.
4. Remaining overlays; then run the lobby path.

## Proof of concept (2026-10-23): one overlay recompiled

The multi-menu overlay (smallest with real code) went through the whole chain locally:
`tools/overlay_to_elf.py --code-only --entry <first function>` wraps header+text in a synthetic ELF;
Ghidra headless imports the **code-only raw image** at the load address (language
`MIPS:LE:64:64-32addr`) and runs the pipeline's `ExportPS2Functions.java`; `ps2_recomp` generates 40 C++
files plus a register file. Lessons:

- Analyze header+text only. With the whole file, Ghidra treated string data as code and the generator failed
  on non-instructions; text size comes from the header, so no heuristic is needed.
- The generator needs an entry function name: use a real function start as the synthetic ELF entry.
- The overlay has an indirect jump (switch); the generator promotes fallback entries with a warning, as it
  may in the main code. Recheck per overlay before trusting it.
- The generated register file targets addresses outside the pinned runtime's table, so it cannot run until the
  runtime patch series (step 3 above) exists.
- Ghidra is not on the default environment: `GHIDRA_HOME` must point at the installed 12.1.x tree.

## Cross-check with the owner's overlay decompilation (2026-10-23)

What the decomp's `*_overlay.c` files are: **word-for-word reassembly**, not decompiled C. Each function is an `__asm__`
block of `.word` instruction values that reproduces the original bytes ("100% matching" is byte identity). They
carry no semantics, so they cannot replace the recompiled overlays or seed native adapters. Their use here is
as an independent check of the overlay images and of function boundaries.

Findings (counts only):

- **Load model confirmed.** The engine's calls into the lobby overlay land on genuine function prologues when the
  whole file, header included, is mapped at the load address; with the header removed they land mid-code. The decomp
  labels addresses relative to the first code word (header of 0x80 bytes stripped), so its VMA is the real address
  minus 0x80 for the menu overlay.
- **Function boundaries.** For the main-menu overlay, 135 of its 144 functions coincide exactly with the
  functions the Ghidra export found (at the 0x80 offset); the export found 28 more. For the other overlays the
  decomp's addresses drift (best offsets differ per overlay, 0x74 to 0x8c), so a straight address comparison is
  not meaningful; do not use decomp addresses for overlays without recomputing them from the file.
- **IOP.** No decompiled IRX/IOP sources were found in the decompilation repository (no files, no commits).

## Resident Decompiled Overlays (2026-10-26)

The static recompilation pipeline for overlays (which generated >12,000 translation units) has been completely replaced by direct compilation of the pure-C decompiled overlays from `@aoe-decomp` (`overlays/src/*.c` and `overlays/include/*_overlay.h`):
- All 8 overlays (`DCOMP=5`, `HIST=9`, `LOBBY=4`, `MO_OV=2`, `MULTIM=7`, `RMMMEM=3`, `SMENU=6`, `THELP=8`) reside simultaneously in host memory (`aoe2_decompiled_overlays` library).
- Three regions (`0x01D98000`, `0x01ED7000`, `0x01F68000`) are registered with `runtime.addOverlayRegion()`.
- Dispatch uses the active overlay ID in guest memory (`0x01D98004`, `0x01ED7004`, `0x01F68004`), dynamically routing guest calls to the correct C implementation via `RegisterGeneratedOverlays()`.
- External guest calls from overlays (`func_00XXXXXX` and `func_01XXXXXX`) are bridged via `overlay_dispatch_guest()`.
- Build times dropped from hours to seconds; all tests and runtime boot pass cleanly.

