# Overlay recompilation: loader findings and plan

Roles only; no guest addresses (provenance rules). Local mapping: `tools/map_overlays.py`.

## How the game loads an overlay (from the owned ELF)

- A per-screen state machine requests an overlay by file name and load address, then
  polls a loader until it reports done. The loader prefixes the language directory, reads the
  **whole file, header included,** to the load address through the disc stack, and invalidates caches.
  Nothing is relocated or decompressed by the loader.
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
