# PS2Recomp runtime dependency inventory

Purpose: inventory the PS2Recomp runtime interfaces and the dedicated fork's 64 MiB guest memory model. API counts below were measured on 2026-10-28; re-measure with §6 before relying on a number. See [documentation index](README.md) and the [RAM expansion plan](plans/nifty-herding-pudding.md).

## 1. Pin and status

- Dedicated fork: `samuelcaldas/PS2Recomp`, license **GPL-3.0**; the authoritative revision is in `third_party_provenance.lock.json`.
- Runtime changes are committed directly in `third_party/PS2Recomp`. No local patch series is applied; bootstrap's `PATCH_DIRECTORIES` is empty.
- Record only a changed fork pin with `python3 tools/bootstrap_dependencies.py --record-current PS2Recomp`; `--check` verifies installed commits against the lockfile without writes. Recording a pin does not publish the fork commit or regenerate game code.

## 2. Generated game code

- `generated/SLES-50282/`: 58,553 `.cpp` units, one per function (generated, not tracked).
- 13,529 of them call the runtime's guest-access macros (`WRITE8/16/32/64/128`, `FAST_WRITE*`). These macros live in `include/ps2_runtime_macros.h` and `include/ps2_runtime.h`.
- Writes go through `ps2TraceGuestWrite` (write-watch) and then `Ps2FastWrite*`, which masks the address with `PS2_RAM_MASK` before indexing `rdram`.
- Generated functions are registered in a table the runtime dispatches through. Our hooks replace entries in that table by address (`replaceFunction`).

**Consequence:** the 32 MB limit is not one constant. It is built into every guest store the generated code performs.

## 3. Runtime API used by this repository

Counts are occurrences outside `third_party/`, `generated/` and `build/`:

| API | Uses | Where it matters |
| :--- | ---: | :--- |
| `PS2Runtime::RecompiledFunction` | 23 | type of every hook |
| `replaceFunction` / `lookupFunction` / `hasFunction` / `registerFunction` | 16 / 5 / 17 / 3 | KCET hook registry, GPU/CDVD/MC/file/mouse/keyboard modules |
| `PS2_REGISTER_GAME_OVERRIDE` | 14 | self-registering game overrides |
| `gs()` | 6 | GS state reads for the GPU snapshot and presentation |
| `eeScheduler()` | 6 | scheduler access in diagnostics and kernel hooks |
| `getIoPaths` / `setIoPaths` | 9 / 1 | disc and ELF directory resolution |
| `loadELF`, `initialize`, `run`, `load`, `store`, `memory`, `cpu` | 1 each | game entry (`platform/common/src/game_main.cpp`) |
| `setMissingFunctionPolicy` | 1 | fail-fast on missing functions |
| `syncCoreSubsystems` | 1 | subsystem sync |
| `presentedFrameExtent` | 1 | our patch `0010` (frame size for mouse mapping) |
| `setPresentationCallback` | **0** | patch `0008` adds it, but no code calls it now |

## 4. Local patch series

| Patch | File(s) touched | Purpose | Used by us? |
| :--- | :--- | :--- | :--- |
| 0001 | `ps2_runtime.h` | overlay dispatch registration | overlays (blocked, see `desktop-runtime-probe.md` §7.1) |
| 0002 | `gs_cpu_backend.cpp` | `PS2X_GS_PRIM_LOG_LIMIT` log cap | diagnostics |
| 0003 | `gs_cpu_backend.cpp` | TEST.ZTE disables depth test | rendering correctness |
| 0004 | `ps2_runtime.cpp` | `PS2X_WRITE_WATCH_LIMIT` report cap | diagnostics |
| 0005 | `ee_scheduler.h`, `EeScheduler.cpp`, `ps2_runtime.cpp` | checkpoint preempt at entry is not a return | scheduler correctness |
| 0006 | `Kernel/Stubs/Helpers/Support.h` | shared CD file registry | CDVD/file modules |
| 0007 | `ps2_runtime.cpp` | window size env and bilinear frame filter | desktop window |
| 0008 | `ps2_runtime.h`, `ps2_runtime.cpp` | presentation callback API | **no caller** (see §3) |
| 0009 | `gs_cpu_backend.cpp`, `gs_frontend.cpp`, `ps2_memory.cpp`, new `gs_frame_stats.h` | `PS2X_GS_FRAME_STATS` evidence | diagnostics |
| 0010 | `ps2_runtime.h`, `ps2_runtime.cpp` | publish presented frame size | yes (mouse mapping) |
| 0011 | `ps2_runtime.cpp`, new `write_watch_summary.h` | `PS2X_WRITE_WATCH_SUMMARY` | diagnostics |

Patch 0008 has no current use. Deciding its retention is a decision for the owner (see `docs/hd-render-evidence.md`).

## 5. Memory and presentation seams (expanded to 64 MB)

- `PS2_RAM_SIZE` (`ps2_memory.h`): Expanded to `64u * 1024u * 1024u` (64 MB).
- `PS2_RAM_MASK`: `0x03FFFFFFu`.
- Guest heap configured in `ps2_runtime.cpp` (`kGuestHeapHardLimit = PS2_RAM_SIZE - 0x00100000u`, `m_asyncCallbackStackFloor = PS2_RAM_SIZE - 0x00100000u`, `m_asyncCallbackStackTop = PS2_RAM_SIZE`).
- SIF HLE masks (`SifCommandEndpoint.cpp`, `SifCdvdServer.cpp`) updated to `PS2_RAM_MASK`.
- DMA chain walking in `ps2_memory.cpp` automatically clamps addresses to the expanded `PS2_RAM_SIZE`.
- The GS VRAM (4 MB) is a separate allocation and is not the guest RAM limit.
- Main repo `kEeRamSize` (`runtime/include/aoe2/runtime/guest_memory.hpp`) and diagnostic snapshots updated to 64 MB (`0x03FFFFFF`).

## 6. How to re-measure

```bash
# runtime API use outside third_party/generated/build
grep -rhoE "runtime(\.|->)[a-zA-Z_]+\(" --include=*.cpp --include=*.hpp --include=*.h game_hooks platform gpu renderer assets input runtime core kernel | sort | uniq -c | sort -rn
# generated units using the guest-access macros
grep -rl "WRITE32(" generated/SLES-50282 | wc -l
# memory-limit uses in the runtime
grep -rn "PS2_RAM_SIZE" third_party/PS2Recomp/ps2xRuntime --include=*.cpp --include=*.h | wc -l
grep -rn "PS2_RAM_MASK" third_party/PS2Recomp/ps2xRuntime --include=*.cpp --include=*.h | wc -l
# patch series parses and applies
python3 -m unittest tests.test_runtime_patches
```

## 7. Open items before removing the 32 MB limit

1. Decide patch `0008` (keep or remove).
2. Confirm the guest code never relies on address wrap-around: no generated code references addresses at or above `0x02000000`, and no `0x1FFFFFF` masks appear (checked 2026-10-28).
3. Measure the real guest RAM budget, including DMA-written regions, since the write-watch measurement does not see DMA.
4. Decide the target map view size from the PC reference (demo binary and partial decomp), not from the PS2 layout.
