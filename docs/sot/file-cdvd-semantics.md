# File and CDVD Subsystems Semantics and Observed Parity

This document records the baseline determinism, function coverage, binary contracts, and observed parity for KCET's file table subsystem (`kt_file`) and CD/DVD server (`kt_libcdvd_server`).

## Architecture and Contracts

- **Memory Models:**
  - `KtFileModel` (`file/include/aoe2/file/kt_file_model.hpp`) operates purely over `aoe2::runtime::GuestAccess`.
  - Global `CdServerState` is located at guest `0x004F1508` (20 bytes: `record_table`, `record_count`, `min_val`, `entry_table`, `entry_count`).
  - Global `CdStreamStatus` is located at guest `0x004F16C0` (12 bytes: `field0`, `active`, `bytes_remaining`).
  - Binary search (`func_0012D128` / `BinarySearch`) operates over 8-byte `FileRecord` structures (`u16 id, u16 aux, u32 payload`), comparing keys against `id & 0x7FFF`.
  - Kind classification (`func_0012CEA0` / `ClassifyKind`) classifies handles into kinds 0..3 (0: flag bits set, 1: exact tag 0x40, 2: other non-zero tags, 3: null pointer).

- **Disc Reading and Streaming Model:**
  - `CdvdModel` (`cdvd/include/aoe2/cdvd/cdvd_model.hpp`) is an isolated, synchronous read model for `<disc_root>/aoe2.iso`, not an extracted-file reader. `QueueRead()` currently has only test callers. It is **not the game's SIF request queue**.
  - CD-ROM sector size is 2048 bytes; `bytes_remaining >> 11` represents remaining sectors. Size arithmetic is widened before multiplication/conversion, and the destination must fit physical 32 MiB EE RAM (including address zero).
  - Queuing updates the three guest status words sequentially; this is not a thread-safe or atomic multiword publication. A zero-sector request is a no-op, even while busy. A second nonempty request raises `std::logic_error` without discarding the first. Oversized signed byte counts raise `std::length_error`; invalid RAM ranges raise `std::out_of_range`, before allocation or I/O.
  - Processing requires a regular, openable ISO, a successful seek and the entire requested byte range. Missing images, EOF crossings, truncation and other I/O failures raise `std::ios_base::failure`. Reading uses a temporary buffer: guest destination and completion status are unchanged on I/O failure, and the pending request remains available for explicit retry or `Reset()`. No zero-filled substitute or raw-ISO EOF padding is accepted.
  - **Actual disc path:** the original guest CDVD routines and SIF `SifCdvdServer::Disc::readToEe` / runtime `readCdSectors()` / `readHostRange()` handle real disc access separately. Their mapped-file padding semantics and short-read behavior are not changed or established by the model tests. The separately tested SIF integration now delivers ordinary RPC completion without writing KCET request entries or signaling the guest request-available semaphore. Overlay dispatch uses the existing registered decompiled overlays; model success, a header write, or an entry lookup does not prove SMENU initialization or rendering.

- **Switches and Game Overrides:**
  - Gated behind switches `file=on` and `cdvd=on` (or `file_cd=on`) in `game/SLES-50282/native_modules.ini`. Default is `off`.
  - Overrides register via `PS2_REGISTER_GAME_OVERRIDE` in `kcet_file_module.cpp` and `kcet_cdvd_module.cpp`.

## Media-check contract (2026-10-26)

The historical `ProcessRequests` entry is a **media classifier**, not the isolated
model's queue dispatcher. The authorized reference is
`samuelcaldas/age-of-empires-2-decomp`, `src/kcet/kt_libcdvd_server.c`, revision
`eb2b12731694310da08d9b6501943169f99f4653`; the current reference checkout was
confirmed to have no changes to that file relative to this revision. The local
generated originals are linked into the opt-in `cdvd_media` regression,
never copied or edited.

The classifier receives a guest structure, preserves its input mask, writes its
media-type and classification fields, and returns the classification. Its early
acceptance route writes zero to both output fields and returns zero regardless
of initial `v0`. The original caller requires DiskReady **2** before classification;
classification **0**, not DiskReady 0, is its acceptance route. The old native void
handler neither calculated the output nor set `v0`, leaving the caller's residual
2. Both the direct-output test and the original-caller test reproduced the defect.

That entry is now `DEFERRED`: native `cdvd`, native `file_cd`, and both-off modes
retain the registered original. A subsequent regression also proved that the
historical native Init was not an initializer: the original maps a requested mode
to its accepted-media mask. Returning `CdvdModel::Reset()`'s zero discards that
mask and makes the original classifier reject even a matching disc. Init is now
`DEFERRED` too; the mode mapping and media classification both remain original.
The initializer test uses an invented guest lookup table, checks both entries and
the original fallback, and never installs a fixed acceptance mask. Bit helpers
and the NCmd getter hook are unchanged.

A further pending-work regression proved that the SCmd getter hook released
waiters before the original worker completed their requests. Disabling native
CDVD during diagnosis let asset transfers advance and the loading text disappear;
the focused test reproduced the early signal independently. The SCmd override is
therefore also deferred: the original getter returns the ID without signaling.
Two consecutive real scheduler waits now remain blocked until a controlled
completion producer signals them, with each native switch and both-off mode.
The previous synchronous-model completion assumption did not describe this guest
queue and is no longer used. The focused tests cover original acceptance, legitimate rejection,
a bounded retry, stack/output guards, callee-saved registers, and real scheduler
semaphore suspension/resumption in controlled external dependencies. This is
contract coverage, **not full CDVD parity or proof that the actual disc is accepted**.

Configure with `-DAOE2_BUILD_CDVD_RUNTIME_TESTS=ON` (off by default); missing owned
generated inputs are an explicit configure error. The target compiles only five originals
(classifier, caller, mode mapping, lookup and SCmd getter) plus the native CDVD
module and test runtime, not the full game.
The `cdvd_model` tests use four patterned synthetic sectors and do not need owned
assets. See [diagnostic results](../scmd-smenu-validation.md) for boot evidence.

## SIF request ownership

The KCET ring is a **request queue**, not a disc-completion queue. Its original
enqueuer publishes a request and signals the request-available semaphore; the
original worker consumes it. The local runtime's legacy `serveCdvd` block instead
invented another entry and signaled that semaphore on every asynchronous RPC
completion. A real SIF DMA regression showed exact synthetic read data being
copied successfully while this unrelated guest queue was modified.

`cmake/Aoe2Sif.cmake` builds a derived runtime SIF source in the build directory,
removing only that verified legacy block. It does not edit `third_party/`, change
RPC result values, replace the sector reader, or add a game queue. Normal SIF
replies and guest callbacks retain responsibility for completion. The bridge
checks the removed block's hash and fails configuration if it changes; an upstream
source without the game injection remains untouched. Python tests cover these
configuration boundaries and upstream immutability. Runtime `cdvd_rpc` tests cover
exact sector bytes, normal completion replies, and no invented requests/signals
with zero, one, or four occupied guest request entries.

This corrects independently reproduced request ownership; it does not claim that
all SIF error reporting, disc truncation, or mapped-file padding is validated.

## Observed Parity (2026-10-25)

A 30-second differential capture under `AOE2_INPUT_SYNTHETIC=1 AOE2_STATE_SNAPSHOT=1` compared the original recompiled baseline (`file=off, cdvd=off`) against the native C++ module implementations (`file=on, cdvd=on`).

The recorded 34 matching snapshots are mouse/keyboard observations. They establish
only input-state agreement during those runs; they do not establish storage or
module parity. No nonempty subsystem-specific packet/state comparison is recorded
here. The unchanged `stalled_in_loop` result does not identify or exclude the cause
of the boot stall. Historical host test counts describe unit coverage only.
