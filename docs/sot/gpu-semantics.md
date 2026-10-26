# Native GPU packet boundary

The native boundary contains exactly seven entries, listed in
`gpu/data/gpu_module_entries.txt`. GPU remains off by default. This implements a
GIF packet writer, not a GS register map or a renderer.

Owner-authorized source: `samuelcaldas/age-of-empires-2-decomp`, commit
`1a32ed5e948d9a3abc7dceae5faf73d8c2f458ed`, `src/kcet/kt_gpu.c`.
Generated units remain ignored, unmodified, and are compiled directly only by the
optional differential test. No generated implementation is copied into tracked source.

`KtGpuModel` uses only `GuestAccess`. Each setter copies the complete 16-byte tag
from `0x38c730`, advances the writer cursor, and stores the tag pointer at context
`+12`. It then appends a 64-bit payload and a zero-extended register selector.
The first three setters read bit 13 of `Load32(data)` after the tag write and add
6, 20, or 22; they read the 64-bit payload at `data+8`. Setter four uses register
`0x3b` and that same payload. Draw uses register `0x1c` and a zero-extended word
at `data+4`; reset writes zero to `0x3f`. Reads are deliberately not hoisted over
packet writes: data, tag, and writer context can alias.

Dispatch validates the byte kind before indexing the 29-entry guest table at
`0x38c740`. Invalid kinds report an assertion and throw; execution never indexes
past the table. With a zero flag it aligns/closes the previous DMA packet and
opens a new one. It invokes the actual guest target through `dispatchGuestBranch`.
The seven-function boundary does not replace the 29 external handler contracts.
Missing handlers fail fast. Finalization computes GIF loop counts, clears the tag
pointer, and pads the cursor to a quadword boundary after handler completion.

The dispatcher adapter saves s0..s3 and ra in the original 80-byte guest stack frame.
The registered continuation at `0x125a4c` restores that frame and finalizes the
packet. The setters retain their original 32/48-byte guest frames and per-call
continuations. A scheduler yield leaves guest pc, stack, and callee context intact;
there is no host-side pending-call slot. Nested calls use separate guest frames.
Installation preflights all seven entries and packet dependencies and rolls back
both continuation and entry replacements on failure.

The external pure-memory tag append, selector and DMA-begin helpers remain generated
and are reached through `dispatchGuestBranch`. This is an explicit boundary exception:
it preserves the upstream eight-cycle dispatch charges and preemption points instead
of treating a whole setter as an uninterruptible host operation. Register emission
and GIF finalization are native. The separate pure model also implements these
packet helpers for contract testing. No SIF/IOP or GS hardware operation is introduced
by this bridge. Timing equivalence is tested under the actual EE scheduler, not
inferred from final packet equality.

## Observer and validation limits

When snapshots are enabled, a single wrapper for each of the seven entries
observes the same **entry** boundary in original and native mode. The call tracer
no longer emits GPU snapshots. Each capture includes the 16-byte writer context
and the actual packet bytes from the open DMA/GIF tag through the current cursor.
Empty writers contain context only; invalid or greater-than-64-KiB ranges produce
explicit skips. Constant dispatch-table bytes are excluded. Entry observation
avoids falsely treating a yielded C++ call as a completed guest call.

The focused `aoe2_gpu_differential_tests` target compiles the selected original
generated functions and helpers, links a real PS2Recomp runtime, and compares
memory effects against the native boundary. It covers setter high bits, aliasing,
all valid table indices with selected generated handlers, flag branches, DMA
alignment/counts, GIF formats/register counts, invalid 29/255, missing installation
entries, suspended calls, independent contexts, and scheduler-driven nested yields.
The table fixtures use the six original setters; this is not exhaustive execution
of all 29 production handlers. The observer fixture requires exactly three matching
GPU records per mode, including nonempty dynamic packet data, without skips or
duplicates. Mouse/keyboard records do not count as GPU parity.

Two full-game desktop captures of 30 seconds each were performed after a successful
Docker diagnostic build, with `gpu=off` and `gpu=on`. The native run installed all
seven entries (`installed 7/7`). Neither run reached a GPU entry: both produced zero
GPU records and zero GPU skips. The visual validators failed in both cases
(`stalled_in_loop`, unchanged frames; three/four real frames respectively, no missing
branch targets). These captures establish installation and bounded runtime behavior,
NOT game-path GPU parity, boot success or rendering/gameplay equivalence. Logs are
preserved locally under `build/gpu-validation/`; configuration was restored byte-exactly.

Fresh main-branch validation: 37/37 host CTests, Python 317 tests with two external
HD-asset skips, and the separate generated-code differential CTest all passed.
The bounded focused comparisons establish packet semantics on their tested vectors:
135 original/native packet comparisons, 60 packet-helper comparisons, three matching
GPU entry records per mode, and 14 scheduler timing/deadline comparisons (all seven
entries with both flag values). The timing regression failed before preserving the
original helper dispatches and passed afterwards. Full packet/context memory is
compared after completion separately from the entry snapshots; guest stack spill
bytes and caller-saved registers are not claimed byte-identical. Stack restoration
and the preserved s0..s3 registers are checked explicitly.

To run with a compatible existing desktop runtime cache:

```sh
cmake -B build/gpu-differential -G Ninja \
  -DAOE2_BUILD_GPU_DIFFERENTIAL_TESTS=ON \
  -DAOE2_GPU_CACHED_BUILD=/absolute/path/to/build/docker-desktop-diagnostics
cmake --build build/gpu-differential --target aoe2_gpu_differential_tests -j 3
ctest --test-dir build/gpu-differential -R '^gpu_differential$' --output-on-failure
```

Without `AOE2_GPU_CACHED_BUILD`, the optional target builds the upstream runtime
through the normal runtime CMake integration. Both modes need the owned generated
units and pinned dependency sources. The cached mode requires ABI-compatible host
libraries and runtime headers; it does not rebuild or modify that cache.
