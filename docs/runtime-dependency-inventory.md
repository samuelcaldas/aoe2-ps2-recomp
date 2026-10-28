# PS2Recomp runtime dependency inventory

Purpose: list what this repository depends on in the upstream PS2Recomp runtime and its local patch series, before any change to the runtime's memory model (for example, removing the 32 MB guest RAM limit). Every count below was measured in the working tree on 2026-10-28; re-measure with the commands in §6 before relying on a number.

## 1. Pin and status

- Upstream: `ran-j/PS2Recomp`, commit `eb8688b2fd191129f103a39543002c005b33409c`, license **GPL-3.0** (`third_party_provenance.lock.json`).
- The checkout in `third_party/PS2Recomp` is the pin plus the tracked patch series in `tools/patches/ps2recomp/`. The pin alone does not reproduce the running runtime (`README.md`).
- Patches apply through `tools/runtime_patches.py`; `tests/test_runtime_patches.py` checks that the series applies and that git can parse it.

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

## 5. Memory and presentation seams (the parts a 64 MB change would touch)

- `PS2_RAM_SIZE` (`ps2_memory.h`): 104 uses under `third_party/PS2Recomp/ps2xRuntime`.
- `PS2_RAM_MASK`: 51 uses in the same tree.
- Guest heap configured in `ps2_runtime.cpp` (`kGuestHeapHardLimit = 0x01F00000`, `m_asyncCallbackStackFloor`, `m_asyncCallbackStackTop = PS2_RAM_SIZE`). The async callback stacks sit at the top of RAM.
- DMA chain walking in `ps2_memory.cpp` clamps addresses to `PS2_RAM_SIZE`.
- The GS VRAM (4 MB) is a separate allocation and is not the guest RAM limit.
- Our map buffer is a constant guest address (`0x01F98000`) passed by game code to the draw-area initializer (`0x31A410`/`0x1AB7E0`). See `docs/hd-render-evidence.md`.
- Measured free space: writes by the game cover `0x1D98000`–`0x1F98000`, so that gap is not free (`docs/hd-render-evidence.md`).

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
