#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <cstring>
#include <iostream>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace {

void EntryHook(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] EntryHook ENTER (fast-forward entry_0x100008)\n", 61);
#endif

    // Fast zero BSS: 0x4E2FE0 to 0x17953A0
    const uint32_t bss_start = 0x4E2FE0 & PS2_RAM_MASK;
    const uint32_t bss_end = 0x17953A0 & PS2_RAM_MASK;
    if (bss_end > bss_start) {
#if defined(__SWITCH__)
        svcOutputDebugString("[aoe2-switch] EntryHook: zeroing BSS\n", 37);
#endif
        std::memset(rdram + bss_start, 0, bss_end - bss_start);
    }

#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] EntryHook: SetupThread syscall 0x3C\n", 50);
#endif
    // Setup registers for SetupThread (syscall 0x3C)
    // a0: gp = 0x4EA7C0, a1: sp = 0x1FE0000, a2: 0x20000, a3: 0x4E3C40, t0: 0x1000C0
    SET_GPR_S32(ctx, 4, 0x4EA7C0); // gp
    SET_GPR_S32(ctx, 5, 0x1FE0000); // sp
    SET_GPR_S32(ctx, 6, 0x20000);   // stack size
    SET_GPR_S32(ctx, 7, 0x4E3C40);  // args
    SET_GPR_S32(ctx, 8, 0x1000C0);  // root
    SET_GPR_U64(ctx, 28, GPR_U64(ctx, 4)); // move $gp, $a0
    SET_GPR_S32(ctx, 3, 0x3C);      // syscall 0x3C
    ctx->pc = 0x100068u;
    runtime->handleSyscall(rdram, ctx, 0x3Cu);

#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] EntryHook: SetupHeap syscall 0x3D\n", 48);
#endif
    SET_GPR_U64(ctx, 29, GPR_U64(ctx, 2)); // move $sp, $v0
    // Setup registers for SetupHeap (syscall 0x3D)
    SET_GPR_S32(ctx, 4, 0x17953A0); // heap base
    SET_GPR_S32(ctx, 5, 0x602C60);  // heap size
    SET_GPR_S32(ctx, 3, 0x3D);      // syscall 0x3D
    ctx->pc = 0x100084u;
    runtime->handleSyscall(rdram, ctx, 0x3Du);

#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] EntryHook: dispatching func_10CB58\n", 49);
#endif
    // Set target PC to func_10CB58 and set return address to 0x10008C
    // Instead of calling dispatchGuestBranch inside EntryHook (which would nest C++ call stacks),
    // we simply update ctx->pc and return so EeScheduler dispatches func_10CB58 at the root frame!
    ctx->pc = 0x10CB58u;
    SET_GPR_U32(ctx, 31, 0x10008Cu);
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] EntryHook: returning to scheduler with pc=0x10CB58\n", 65);
#endif
}

void InstallEntryHook(PS2Runtime& runtime) {
    if (runtime.hasFunction(0x100008u)) {
        runtime.replaceFunction(0x100008u, EntryHook);
        std::cout << "[aoe2] native entry hook installed at 0x100008\n";
#if defined(__SWITCH__)
        svcOutputDebugString("[aoe2-switch] native entry hook installed at 0x100008\n", 53);
#endif
    }
}

} // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native entry hook", "AOK.ELF", 0, 0, InstallEntryHook)
