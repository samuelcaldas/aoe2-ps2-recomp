#pragma once
// Semantic reference: samuelcaldas/age-of-empires-2-decomp, src/kcet/kt_hd.c,
// commit 81a9dda41511ea5af676566cf23e14dca390cbac. Guest frames and dispatch
// continuations follow the owned binary when the C reference omits ABI details.
#include "guest_rdram_access.hpp"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <array>
#include <stdexcept>

namespace aoe2::game_hooks::hd {
// Runtime-only operations: guest stack frames carry every suspended call's state.
inline uint64_t Load64(RdramGuestAccess& m, uint32_t p) {
    return m.Load32(p) | (uint64_t(m.Load32(p + 4)) << 32);
}
inline void Store64(RdramGuestAccess& m, uint32_t p, uint64_t v) {
    m.Store32(p, uint32_t(v)); m.Store32(p + 4, uint32_t(v >> 32));
}
template<uint32_t ReturnBranch>
void PopReturn(uint8_t* ram, R5900Context* c, PS2Runtime*) {
    RdramGuestAccess m; m.Bind(ram);
    const auto sp = getRegU32(c, 29);
    SET_GPR_U64(c, 31, Load64(m, sp));
    SET_GPR_S32(c, 29, int32_t(sp + 16));
    c->branch_pc = ReturnBranch;
    c->in_delay_slot = false;
    c->pc = getRegU32(c, 31);
}
inline void Callback(uint8_t* ram, R5900Context* c, PS2Runtime* r) {
    RdramGuestAccess m; m.Bind(ram);
    const auto sp = getRegU32(c, 29) - 16;
    SET_GPR_S32(c, 29, int32_t(sp));
    SET_GPR_U64(c, 2, GPR_U64(c, 4));
    Store64(m, sp, GPR_U64(c, 31));
    SET_GPR_U64(c, 4, GPR_U64(c, 5));
    c->branch_pc = 0x12956c;
    c->in_delay_slot = false;
    if (GPR_U64(c, 2)) {
        const auto target = getRegU32(c, 2);
        SET_GPR_U32(c, 31, 0x12957c);
        if (!r->dispatchGuestBranch(ram, c, target, 0x129574, 0x12957c,
                PS2Runtime::GuestBranchKind::IndirectCall, "native HD callback")) return;
    }
    PopReturn<0x129580>(ram, c, r);
}
inline void SemaFinish(uint8_t* ram, R5900Context* c, PS2Runtime*) {
    RdramGuestAccess m; m.Bind(ram);
    const auto sp = getRegU32(c, 29);
    SET_GPR_U64(c, 2, GPR_U64(c, 16) ^ GPR_U64(c, 2));
    if (GPR_U64(c, 2)) SET_GPR_VEC(c, 17, GPR_VEC(c, 0));
    SET_GPR_U64(c, 2, GPR_U64(c, 17));
    SET_GPR_U64(c, 31, Load64(m, sp + 48));
    SET_GPR_U64(c, 17, Load64(m, sp + 32));
    SET_GPR_U64(c, 16, Load64(m, sp + 16));
    SET_GPR_S32(c, 29, int32_t(sp + 64));
    c->branch_pc = 0x129498;
    c->in_delay_slot = false;
    c->pc = getRegU32(c, 31);
}
inline void Sema(uint8_t* ram, R5900Context* c, PS2Runtime* r) {
    RdramGuestAccess m; m.Bind(ram);
    const auto sp = getRegU32(c, 29) - 64;
    SET_GPR_S32(c, 29, int32_t(sp));
    SET_GPR_S32(c, 3, 0x4e0000);
    Store64(m, sp + 16, GPR_U64(c, 16));
    SET_GPR_U64(c, 5, GPR_U64(c, 4));
    Store64(m, sp + 32, GPR_U64(c, 17));
    SET_GPR_S32(c, 2, -1);
    Store64(m, sp + 48, GPR_U64(c, 31));
    SET_GPR_S32(c, 16, int32_t(m.Load32(getRegU32(c, 28) - 0x7bd8)));
    SET_GPR_S32(c, 9, 0x4e2c28);
    SET_GPR_U64(c, 6, Load64(m, 0x4e2c28));
    Store64(m, sp, GPR_U64(c, 6));
    SET_GPR_S32(c, 17, 1);
    c->branch_pc = 0x12945c;
    c->in_delay_slot = false;
    if (GPR_U64(c, 16) != GPR_U64(c, 2)) {
        if (GPR_U64(c, 5)) SET_GPR_VEC(c, 4, GPR_VEC(c, 17));
        SET_GPR_U64(c, 5, GPR_U64(c, 4));
        SET_GPR_S32(c, 2, int32_t(getRegU32(c, 5) << 2));
        SET_GPR_S32(c, 3, int32_t(sp + getRegU32(c, 2)));
        SET_GPR_S32(c, 2, int32_t(m.Load32(getRegU32(c, 3))));
        const auto target = getRegU32(c, 2);
        SET_GPR_U32(c, 31, 0x129480);
        SET_GPR_U64(c, 4, GPR_U64(c, 16));
        c->branch_pc = 0x129478;
        if (!r->dispatchGuestBranch(ram, c, target, 0x129478, 0x129480,
                PS2Runtime::GuestBranchKind::IndirectCall, "native HD semaphore")) return;
    } else {
        // No guest operation when the stored semaphore ID is -1.
        SET_GPR_U64(c, 2, GPR_U64(c, 16)); // SemaFinish's comparison must preserve s1=1.
    }
    SemaFinish(ram, c, r);
}
inline void SemaNonzero(uint8_t* ram, R5900Context* c, PS2Runtime* r) {
    RdramGuestAccess m; m.Bind(ram);
    const auto sp = getRegU32(c, 29) - 16;
    SET_GPR_S32(c, 29, int32_t(sp));
    Store64(m, sp, GPR_U64(c, 31));
    SET_GPR_U32(c, 31, 0x1294d0);
    SET_GPR_S32(c, 4, 1);
    c->branch_pc = 0x1294c8;
    c->in_delay_slot = false;
    if (!r->dispatchGuestBranch(ram, c, 0x129428, 0x1294c8, 0x1294d0,
            PS2Runtime::GuestBranchKind::DirectCall, "native HD semaphore wrapper")) return;
    PopReturn<0x1294d4>(ram, c, r);
}
inline constexpr std::array<uint32_t, 3> entries{0x129560, 0x129428, 0x1294c0};
inline constexpr std::array<PS2Runtime::RecompiledFunction, 3> handlers{Callback, Sema, SemaNonzero};
inline constexpr std::array<uint32_t, 3> returns{0x12957c, 0x129480, 0x1294d0};
inline constexpr std::array<PS2Runtime::RecompiledFunction, 3> continuations{
    PopReturn<0x129580>, SemaFinish, PopReturn<0x1294d4>};
inline void Install(PS2Runtime& r) {
    std::array<PS2Runtime::RecompiledFunction, 3> oldEntries{}, oldReturns{};
    for (unsigned i = 0; i < entries.size(); ++i) {
        if (!r.hasFunction(entries[i])) throw std::runtime_error("native HD: missing runtime entry");
        oldEntries[i] = r.lookupFunction(entries[i]);
        oldReturns[i] = r.hasFunction(returns[i]) ? r.lookupFunction(returns[i]) : nullptr;
    }
    unsigned installedReturns = 0, installedEntries = 0;
    try {
        for (; installedReturns < returns.size(); ++installedReturns)
            if (!r.registerFunction(returns[installedReturns], continuations[installedReturns]))
                throw std::runtime_error("native HD: continuation registration failed");
        for (; installedEntries < entries.size(); ++installedEntries)
            if (!r.replaceFunction(entries[installedEntries], handlers[installedEntries]))
                throw std::runtime_error("native HD: replacement failed");
    } catch (...) {
        for (unsigned i = 0; i < installedEntries; ++i) r.replaceFunction(entries[i], oldEntries[i]);
        for (unsigned i = 0; i < installedReturns; ++i) r.registerFunction(returns[i], oldReturns[i]);
        throw;
    }
}
}
