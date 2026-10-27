#pragma once
#include <aoe2/gpu/kt_gpu_model.hpp>
#include "guest_rdram_access.hpp"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include <array>
#include <stdexcept>

// Owner-authorized kt_gpu adaptation: samuelcaldas/age-of-empires-2-decomp,
// commit 1a32ed5e948d9a3abc7dceae5faf73d8c2f458ed.
namespace aoe2::game_hooks::gpu {
inline constexpr std::array<uint32_t, 7> entries{
    0x125808, 0x125858, 0x1258a8, 0x1258f8, 0x125940, 0x125988, 0x1259c8};

using NativeDrawInterceptor = bool (*)(uint8_t kind, uint32_t cmd_addr, uint32_t work_state_addr);
inline NativeDrawInterceptor g_native_draw_interceptor = nullptr;

inline void SetNativeDrawInterceptor(NativeDrawInterceptor interceptor) {
    g_native_draw_interceptor = interceptor;
}

inline uint64_t Load64(RdramGuestAccess& m, uint32_t p) {
    const auto low = m.Load32(p);
    return low | (uint64_t(m.Load32(p + 4)) << 32);
}
inline void Store64(RdramGuestAccess& m, uint32_t p, uint64_t value) {
    m.Store32(p, uint32_t(value));
    m.Store32(p + 4, uint32_t(value >> 32));
}
inline void Resume(uint8_t* ram, R5900Context* ctx, PS2Runtime*) {
    RdramGuestAccess memory; memory.Bind(ram);
    const auto p = getRegU32(ctx, 16), sp = getRegU32(ctx, 29);
    SET_GPR_U64(ctx, 31, Load64(memory, sp + 64));
    for (int reg = 19; reg >= 16; --reg)
        SET_GPR_U64(ctx, reg, Load64(memory, sp + (reg - 16) * 16));
    SET_GPR_S32(ctx, 29, int32_t(sp + 80));
    aoe2::gpu::KtGpuModel(memory).Finalize(p);
    ctx->pc = getRegU32(ctx, 31);
}
inline void DispatchHandler(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    RdramGuestAccess memory; memory.Bind(ram);
    const uint8_t kind = uint8_t(getRegU32(ctx, 18));
    const uint32_t cmd_addr = getRegU32(ctx, 19);
    const uint32_t work_state = getRegU32(ctx, 16);

    if (g_native_draw_interceptor && g_native_draw_interceptor(kind, cmd_addr, work_state)) {
        Resume(ram, ctx, runtime);
        return;
    }

    const auto target = aoe2::gpu::KtGpuModel(memory).Handler(kind);
    if (!runtime->hasFunction(target)) throw std::runtime_error("native GPU: missing guest handler");
    SET_GPR_U64(ctx, 4, GPR_U64(ctx, 16));
    SET_GPR_U64(ctx, 5, GPR_U64(ctx, 19));
    SET_GPR_U32(ctx, 31, 0x125a4c);
    if (!runtime->dispatchGuestBranch(ram, ctx, target, 0x125a44, 0x125a4c,
            PS2Runtime::GuestBranchKind::IndirectCall, "native GPU handler")) return;
    Resume(ram, ctx, runtime);
}
inline void Dispatch(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    RdramGuestAccess memory; memory.Bind(ram);
    const auto command = getRegU32(ctx, 5), sp = getRegU32(ctx, 29) - 80;
    SET_GPR_S32(ctx, 29, int32_t(sp));
    Store64(memory, sp + 48, GPR_U64(ctx, 19));
    Store64(memory, sp + 16, GPR_U64(ctx, 17));
    SET_GPR_U64(ctx, 19, GPR_U64(ctx, 5));
    Store64(memory, sp, GPR_U64(ctx, 16));
    SET_GPR_U64(ctx, 17, GPR_U64(ctx, 6));
    Store64(memory, sp + 64, GPR_U64(ctx, 31));
    Store64(memory, sp + 32, GPR_U64(ctx, 18));
    SET_GPR_U64(ctx, 18, aoe2::gpu::KtGpuModel(memory).ValidateCommand(command));
    SET_GPR_U64(ctx, 16, GPR_U64(ctx, 4));
    if (!GPR_U64(ctx, 17)) {
        SET_GPR_U64(ctx, 5, 0); SET_GPR_U64(ctx, 6, 0); SET_GPR_U64(ctx, 7, 0);
        SET_GPR_U32(ctx, 31, 0x125a2c);
        if (!runtime->dispatchGuestBranch(ram, ctx, 0x104448, 0x125a24, 0x125a2c,
                PS2Runtime::GuestBranchKind::DirectCall, "native GPU DMA begin")) return;
    }
    DispatchHandler(ram, ctx, runtime);
}
inline constexpr std::array<uint32_t, 6> tagReturns{
    0x125830, 0x125880, 0x1258d0, 0x125920, 0x125968, 0x1259a8};
inline constexpr std::array<uint32_t, 3> selectorReturns{0x125838, 0x125888, 0x1258d8};

template<int Slot>
void FinishSetter(uint8_t* ram, R5900Context* ctx, PS2Runtime*) {
    RdramGuestAccess memory; memory.Bind(ram);
    const auto p = getRegU32(ctx, 16), data = getRegU32(ctx, 17), sp = getRegU32(ctx, 29);
    constexpr std::array<uint32_t, 6> bases{6, 20, 22, 0x3b, 0x1c, 0x3f};
    const auto reg = bases[Slot] + (Slot < 3 ? getRegU32(ctx, 2) : 0);
    uint64_t value = 0;
    if constexpr (Slot < 4) value = Load64(memory, data + 8);
    if constexpr (Slot == 4) value = memory.Load32(data + 4);
    SET_GPR_U64(ctx, 31, Load64(memory, sp + (Slot == 5 ? 16 : 32)));
    if constexpr (Slot != 5) SET_GPR_U64(ctx, 17, Load64(memory, sp + 16));
    SET_GPR_U64(ctx, 16, Load64(memory, sp));
    SET_GPR_S32(ctx, 29, int32_t(sp + (Slot == 5 ? 32 : 48)));
    SET_GPR_U32(ctx, 4, p); SET_GPR_U32(ctx, 5, reg); SET_GPR_U64(ctx, 6, value);
    aoe2::gpu::KtGpuModel(memory).Register(p, reg, value);
    ctx->pc = getRegU32(ctx, 31);
}
template<int Slot>
void AfterTag(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    if constexpr (Slot < 3) {
        SET_GPR_U64(ctx, 4, GPR_U64(ctx, 17));
        SET_GPR_U32(ctx, 31, selectorReturns[Slot]);
        if (!runtime->dispatchGuestBranch(ram, ctx, 0x123a70, tagReturns[Slot], selectorReturns[Slot],
                PS2Runtime::GuestBranchKind::DirectCall, "native GPU context selector")) return;
    }
    FinishSetter<Slot>(ram, ctx, runtime);
}
template<int Slot>
void Entry(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    if constexpr (Slot == 6) { Dispatch(ram, ctx, runtime); }
    if constexpr (Slot < 6) {
        RdramGuestAccess memory; memory.Bind(ram);
        const auto sp = getRegU32(ctx, 29) - (Slot == 5 ? 32 : 48);
        SET_GPR_S32(ctx, 29, int32_t(sp));
        if constexpr (Slot != 5) Store64(memory, sp + 16, GPR_U64(ctx, 17));
        Store64(memory, sp, GPR_U64(ctx, 16));
        if constexpr (Slot != 5) SET_GPR_U64(ctx, 17, GPR_U64(ctx, 5));
        Store64(memory, sp + (Slot == 5 ? 16 : 32), GPR_U64(ctx, 31));
        SET_GPR_U64(ctx, 16, GPR_U64(ctx, 4));
        std::array<uint8_t, 16> tag{};
        memory.ReadBytes(0x38c730, tag.data(), tag.size());
        std::memcpy(&ctx->r[5], tag.data(), tag.size());
        SET_GPR_U32(ctx, 31, tagReturns[Slot]);
        if (!runtime->dispatchGuestBranch(ram, ctx, 0x1045e8, tagReturns[Slot] - 8, tagReturns[Slot],
                PS2Runtime::GuestBranchKind::DirectCall, "native GPU tag append")) return;
        AfterTag<Slot>(ram, ctx, runtime);
    }
}
inline constexpr std::array<PS2Runtime::RecompiledFunction, 7> handlers{
    Entry<0>, Entry<1>, Entry<2>, Entry<3>, Entry<4>, Entry<5>, Entry<6>};
inline constexpr std::array<uint32_t, 11> continuationEntries{
    0x125a4c, 0x125a2c, 0x125830, 0x125880, 0x1258d0, 0x125920,
    0x125968, 0x1259a8, 0x125838, 0x125888, 0x1258d8};
inline constexpr std::array<PS2Runtime::RecompiledFunction, 11> continuations{
    Resume, DispatchHandler, AfterTag<0>, AfterTag<1>, AfterTag<2>, AfterTag<3>,
    AfterTag<4>, AfterTag<5>, FinishSetter<0>, FinishSetter<1>, FinishSetter<2>};
inline void Install(PS2Runtime& runtime) {
    for (const auto address : entries)
        if (!runtime.hasFunction(address)) throw std::runtime_error("native GPU: incomplete entry inventory");
    for (const auto address : {0x1045e8u, 0x123a70u, 0x104448u, 0x1043d8u})
        if (!runtime.hasFunction(address)) throw std::runtime_error("native GPU: missing packet dependency");
    std::array<PS2Runtime::RecompiledFunction, continuationEntries.size()> previousContinuations{};
    std::array<PS2Runtime::RecompiledFunction, entries.size()> previousEntries{};
    for (std::size_t i = 0; i < entries.size(); ++i) previousEntries[i] = runtime.lookupFunction(entries[i]);
    for (std::size_t i = 0; i < continuationEntries.size(); ++i) {
        const auto address = continuationEntries[i];
        previousContinuations[i] = runtime.hasFunction(address) ? runtime.lookupFunction(address) : nullptr;
        if (runtime.registerFunction(address, continuations[i])) continue;
        for (std::size_t j = 0; j < i; ++j) runtime.registerFunction(continuationEntries[j], previousContinuations[j]);
        throw std::runtime_error("native GPU: continuation registration unsupported");
    }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (runtime.replaceFunction(entries[i], handlers[i])) continue;
        for (std::size_t j = 0; j < i; ++j) runtime.replaceFunction(entries[j], previousEntries[j]);
        for (std::size_t j = 0; j < continuationEntries.size(); ++j)
            runtime.registerFunction(continuationEntries[j], previousContinuations[j]);
        throw std::runtime_error("native GPU: entry replacement failed");
    }
}
}
