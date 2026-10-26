#include "gpu_runtime_adapter.hpp"
#include "gpu_snapshot.hpp"
#include "game_overrides.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include "runtime/ee_scheduler.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

#define ORIGINAL(a) extern void FUN_00##a##_0x##a(uint8_t*, R5900Context*, PS2Runtime*);
ORIGINAL(125808) ORIGINAL(125858) ORIGINAL(1258a8) ORIGINAL(1258f8)
ORIGINAL(125940) ORIGINAL(125988) ORIGINAL(1259c8)
ORIGINAL(1043d8) ORIGINAL(104448) ORIGINAL(1045e8) ORIGINAL(104600)
ORIGINAL(1046a8) ORIGINAL(123a70)
#undef ORIGINAL
extern PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[];
namespace {
using namespace aoe2::game_hooks;
constexpr uint32_t P = 0x2000, DATA = 0x3000, OUT = 0x4000, STACK = 0x100000;
constexpr uint32_t RETURN = 0x8000, CALLBACK = 0x9000, CALLBACK_RESUME = 0x9004;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
const std::array<PS2Runtime::RecompiledFunction, 7> original{
    FUN_00125808_0x125808, FUN_00125858_0x125858, FUN_001258a8_0x1258a8,
    FUN_001258f8_0x1258f8, FUN_00125940_0x125940, FUN_00125988_0x125988,
    FUN_001259c8_0x1259c8};
void RegisterOriginals(PS2Runtime& runtime) {
    for (unsigned i = 0; i < 7; ++i) runtime.registerFunction(gpu::entries[i], original[i]);
#define REGISTER(a) runtime.registerFunction(0x##a, FUN_00##a##_0x##a)
    REGISTER(1043d8); REGISTER(104448); REGISTER(1045e8); REGISTER(104600);
    REGISTER(1046a8); REGISTER(123a70);
#undef REGISTER
    runtime.registerFunction(0x125a4c, original[6]);
    runtime.registerFunction(0x125a2c, original[6]);
    for (unsigned i = 0; i < 6; ++i) runtime.registerFunction(gpu::tagReturns[i], original[i]);
    for (unsigned i = 0; i < 3; ++i) runtime.registerFunction(gpu::selectorReturns[i], original[i]);
    runtime.registerFunction(0x104474, FUN_00104448_0x104448);
    runtime.registerFunction(0x1043e8, FUN_001043d8_0x1043d8);
    runtime.registerFunction(0x104680, FUN_00104600_0x104600);
}
void Prepare(uint8_t* ram, R5900Context& ctx, uint32_t data, uint32_t cursor) {
    std::memset(ram, 0, 0x2000000);
    ctx = {};
    RdramGuestAccess mem; mem.Bind(ram);
    mem.Store32(P, cursor);
    mem.Store32(0x38c730, 0); mem.Store32(0x38c734, 0x10000000);
    mem.Store32(0x38c738, 0x0e); mem.Store32(0x38c73c, 0);
    mem.Store32(data, 0x2000);
    mem.Store32(data + 4, 0xfedcba98);
    mem.Store32(data + 8, 0x76543210); mem.Store32(data + 12, 0xfedcba98);
    SET_GPR_U32(&ctx, 4, P); SET_GPR_U32(&ctx, 5, data);
    SET_GPR_U32(&ctx, 29, STACK); SET_GPR_U32(&ctx, 31, RETURN);
    for (int reg = 16; reg < 20; ++reg) SET_GPR_U64((&ctx), reg, 0xabcdef1234560000ull + reg);
}
void RunToReturn(PS2Runtime& runtime, R5900Context& ctx, PS2Runtime::RecompiledFunction fn) {
    auto ram = runtime.memory().getRDRAM();
    fn(ram, &ctx, &runtime);
    for (unsigned steps = 0; ctx.pc != RETURN && steps < 100; ++steps) {
        const auto next = runtime.lookupFunction(ctx.pc);
        Check(next != nullptr, "missing continuation");
        next(ram, &ctx, &runtime);
    }
    Check(ctx.pc == RETURN, "guest did not complete");
    Check(getRegU32(&ctx, 29) == STACK, "stack not restored");
    for (int reg = 16; reg < 20; ++reg)
        Check(GPR_U64((&ctx), reg) == 0xabcdef1234560000ull + reg, "callee-saved register corrupted");
}
std::vector<uint8_t> PacketState(uint8_t* ram) {
    std::vector<uint8_t> result(ram + 0x1000, ram + 0x5000);
    result.insert(result.end(), ram + 0x38c730, ram + 0x38c830);
    return result;
}
void Differential(PS2Runtime& runtime) {
    auto ram = runtime.memory().getRDRAM();
    R5900Context ctx{};
    unsigned comparisons = 0;
    for (unsigned slot = 0; slot < 6; ++slot)
        for (unsigned selector : {0u, 1u})
        for (uint32_t data : {DATA, OUT, OUT + 16, P}) {
            RegisterOriginals(runtime);
            Prepare(ram, ctx, data, OUT);
            ram[data + 1] = uint8_t((ram[data + 1] & ~0x20u) | (selector << 5));
            ctx.pc = gpu::entries[slot];
            RunToReturn(runtime, ctx, original[slot]);
            const auto expected = PacketState(ram);
            Prepare(ram, ctx, data, OUT);
            ram[data + 1] = uint8_t((ram[data + 1] & ~0x20u) | (selector << 5));
            ctx.pc = gpu::entries[slot];
            RunToReturn(runtime, ctx, gpu::handlers[slot]);
            Check(expected == PacketState(ram), "setter differential packet/context mismatch");
            ++comparisons;
        }
    // All table kinds, both flag branches, preexisting DMA counts and alignment padding.
    for (unsigned kind = 0; kind < 29; ++kind) for (unsigned flag : {0u, 1u, 0xffffffffu}) {
        std::vector<uint8_t> expected;
        for (bool native : {false, true}) {
            RegisterOriginals(runtime);
            Prepare(ram, ctx, DATA, OUT + 4);
            RdramGuestAccess mem; mem.Bind(ram);
            mem.Store8(DATA, kind);
            for (unsigned i = 0; i < 29; ++i) mem.Store32(0x38c740 + i * 4, gpu::entries[i % 6]);
            mem.Store32(P + 8, OUT - 16);
            mem.Store32(OUT - 16, 0x10000003);
            SET_GPR_U32(&ctx, 6, flag);
            ctx.pc = gpu::entries[6];
            if (native) gpu::Install(runtime);
            RunToReturn(runtime, ctx, runtime.lookupFunction(gpu::entries[6]));
            const auto state = PacketState(ram);
            if (!native) expected = state;
            else Check(expected == state, "dispatcher differential packet/context mismatch");
        }
        ++comparisons;
    }
    std::cout << comparisons << " generated/native packet comparisons passed\n";
}
void HelperDifferential(PS2Runtime& runtime) {
    auto ram = runtime.memory().getRDRAM();
    R5900Context ctx{};
    unsigned comparisons = 0;
    for (unsigned offset : {0, 4, 8, 12}) for (uint32_t previous : {0u, OUT - 16, P + 8}) {
        std::vector<uint8_t> expected;
        for (bool native : {false, true}) {
            RegisterOriginals(runtime);
            Prepare(ram, ctx, DATA, OUT + offset);
            SET_GPR_U32(&ctx, 5, 0);
            RdramGuestAccess mem; mem.Bind(ram);
            mem.Store32(P + 8, previous);
            if (previous == OUT - 16) mem.Store32(previous, 0x10000003);
            if (native) aoe2::gpu::KtGpuModel(mem).Begin(P);
            else RunToReturn(runtime, ctx, FUN_00104448_0x104448);
            if (!native) expected = PacketState(ram);
            else {
                const auto actual = PacketState(ram);
                if (expected != actual) {
                    const auto mismatch = std::mismatch(expected.begin(), expected.end(), actual.begin());
                    std::cerr << "begin offset=" << offset << " previous=" << std::hex << previous
                              << " address=" << (0x1000 + (mismatch.first - expected.begin()))
                              << " expected=" << unsigned(*mismatch.first)
                              << " actual=" << unsigned(*mismatch.second) << std::dec << '\n';
                }
                Check(expected == actual, "begin helper differential mismatch");
            }
        }
        ++comparisons;
    }
    for (unsigned format = 0; format < 4; ++format)
        for (unsigned registers : {0, 1, 3}) for (unsigned offset : {0, 4, 8, 12}) {
            std::vector<uint8_t> expected;
            for (bool native : {false, true}) {
                Prepare(ram, ctx, DATA, OUT + 32 + offset);
                RdramGuestAccess mem; mem.Bind(ram);
                mem.Store32(P + 12, OUT);
                gpu::Store64(mem, OUT, (uint64_t(format) << 58) | (uint64_t(registers) << 60) | 0x8000);
                if (native) aoe2::gpu::KtGpuModel(mem).Finalize(P);
                else RunToReturn(runtime, ctx, FUN_00104600_0x104600);
                if (!native) expected = PacketState(ram);
                else Check(expected == PacketState(ram), "finalize helper differential mismatch");
            }
            ++comparisons;
        }
    std::cout << comparisons << " generated/native packet helper comparisons passed\n";
}
int callbackCalls = 0, resumeCalls = 0;
void Yielding(uint8_t*, R5900Context* ctx, PS2Runtime* runtime) {
    ++callbackCalls;
    ctx->pc = CALLBACK_RESUME; // Actual suspended guest work, no successful return yet.
    if (runtime->eeScheduler().isExecutingGuest()) {
        runtime->eeScheduler().scheduleHostCallback(1, [] {});
        runtime->eeScheduler().accountCycles(2);
        Check(runtime->eeScheduler().checkpointDue(), "scheduler did not yield");
    }
}
void Complete(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    ++resumeCalls;
    gpu::Entry<5>(ram, ctx, runtime);
}
void Continuation(PS2Runtime& runtime) {
    RegisterOriginals(runtime);
    gpu::Install(runtime);
    runtime.registerFunction(CALLBACK, Yielding);
    runtime.registerFunction(CALLBACK_RESUME, Complete);
    auto ram = runtime.memory().getRDRAM();
    R5900Context ctx{};
    Prepare(ram, ctx, DATA, OUT);
    RdramGuestAccess mem; mem.Bind(ram);
    mem.Store8(DATA, 0); mem.Store32(0x38c740, CALLBACK);
    SET_GPR_U32(&ctx, 6, 0);
    gpu::Dispatch(ram, &ctx, &runtime);
    Check(ctx.pc == CALLBACK_RESUME && callbackCalls == 1, "yield was not preserved");
    Check(getRegU32(&ctx, 29) == STACK - 80 && mem.Load32(P) == OUT + 16,
          "yield prematurely finalized or restored stack");
    // Simulate another thread's complete call while the first context is suspended.
    R5900Context other{};
    SET_GPR_U32(&other, 4, P + 0x100); SET_GPR_U32(&other, 5, DATA + 0x100);
    SET_GPR_U32(&other, 6, 0); SET_GPR_U32(&other, 29, STACK + 0x1000);
    SET_GPR_U32(&other, 31, RETURN);
    mem.Store32(P + 0x100, OUT + 0x100);
    mem.Store8(DATA + 0x100, 1); mem.Store32(0x38c744, gpu::entries[5]);
    gpu::Dispatch(ram, &other, &runtime);
    Check(other.pc == RETURN && getRegU32(&other, 29) == STACK + 0x1000, "other thread damaged");
    RunToReturn(runtime, ctx, Complete);
    Check(resumeCalls == 1 && callbackCalls == 1 && mem.Load32(P + 12) == 0,
          "continuation duplicated or failed to finalize");
    for (unsigned kind : {29, 255}) {
        Prepare(ram, ctx, DATA, OUT);
        mem.Store8(DATA, kind);
        bool rejected = false;
        try { gpu::Dispatch(ram, &ctx, &runtime); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && mem.Load32(P) == OUT, "invalid dispatch emitted packet");
    }
}
constexpr uint32_t NESTED = 0x9010, NESTED_RESUME = 0x9014;
void NestedComplete(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    RdramGuestAccess mem; mem.Bind(ram);
    const auto sp = getRegU32(ctx, 29);
    SET_GPR_U64(ctx, 31, gpu::Load64(mem, sp));
    SET_GPR_S32(ctx, 29, int32_t(sp + 16));
    SET_GPR_U32(ctx, 4, P);
    gpu::Entry<5>(ram, ctx, runtime);
}
void Nested(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    RdramGuestAccess mem; mem.Bind(ram);
    const auto sp = getRegU32(ctx, 29) - 16;
    gpu::Store64(mem, sp, GPR_U64(ctx, 31));
    SET_GPR_S32(ctx, 29, int32_t(sp));
    SET_GPR_U32(ctx, 4, P + 0x100); SET_GPR_U32(ctx, 5, DATA + 0x100);
    SET_GPR_U32(ctx, 6, 0); SET_GPR_U32(ctx, 31, NESTED_RESUME);
    if (!runtime->dispatchGuestBranch(ram, ctx, gpu::entries[6], NESTED,
            NESTED_RESUME, PS2Runtime::GuestBranchKind::DirectCall, "nested GPU test")) return;
    NestedComplete(ram, ctx, runtime);
}
bool schedulerReturned = false;
void SchedulerReturn(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    Check(getRegU32(ctx, 29) == STACK, "scheduler stack not restored");
    for (int reg = 16; reg < 20; ++reg)
        Check(GPR_U64(ctx, reg) == 0xabcdef1234560000ull + reg, "scheduler callee context damaged");
    RdramGuestAccess mem; mem.Bind(ram);
    Check(mem.Load32(P + 12) == 0 && mem.Load32(P + 0x10c) == 0, "nested packet not finalized");
    Check(mem.Load32(P) == OUT + 48 && mem.Load32(P + 0x100) == OUT + 0x130,
          "nested packets have wrong cursor");
    schedulerReturned = true;
    ctx->pc = 0;
    runtime->eeScheduler().requestStop();
}
void SchedulerAndNested(PS2Runtime& runtime) {
    Check(runtime.syncCoreSubsystems(), "scheduler initialization failed");
    RegisterOriginals(runtime);
    gpu::Install(runtime);
    runtime.registerFunction(NESTED, Nested);
    runtime.registerFunction(NESTED_RESUME, NestedComplete);
    runtime.registerFunction(CALLBACK, Yielding);
    runtime.registerFunction(CALLBACK_RESUME, Complete);
    runtime.registerFunction(RETURN, SchedulerReturn);
    auto ram = runtime.memory().getRDRAM();
    R5900Context ctx{};
    Prepare(ram, ctx, DATA, OUT);
    RdramGuestAccess mem; mem.Bind(ram);
    mem.Store8(DATA, 0); mem.Store32(0x38c740, NESTED);
    mem.Store8(DATA + 0x100, 1); mem.Store32(0x38c744, CALLBACK);
    mem.Store32(P + 0x100, OUT + 0x100);
    SET_GPR_U32(&ctx, 6, 0);
    ctx.pc = gpu::entries[6];
    callbackCalls = resumeCalls = 0;
    runtime.eeScheduler().reset(ram, ctx);
    runtime.eeScheduler().run();
    Check(schedulerReturned && callbackCalls == 1 && resumeCalls == 1,
          "scheduler nested yield did not complete exactly once");
}
uint64_t completedCycles = 0;
void CycleReturn(uint8_t*, R5900Context* ctx, PS2Runtime* runtime) {
    runtime->eeScheduler().publishSnapshot();
    completedCycles = runtime->eeScheduler().snapshot().eeCycle;
    ctx->pc = 0;
    runtime->eeScheduler().requestStop();
}
void SchedulerCycleParity(PS2Runtime& runtime) {
    Check(runtime.syncCoreSubsystems(), "cycle test scheduler init");
    for (unsigned slot = 0; slot < 7; ++slot) for (unsigned flag : {0u, 1u}) {
        std::vector<uint64_t> expected;
        for (bool native : {false, true}) {
            RegisterOriginals(runtime);
            if (native) gpu::Install(runtime);
            runtime.registerFunction(RETURN, CycleReturn);
            auto ram = runtime.memory().getRDRAM();
            R5900Context ctx{};
            Prepare(ram, ctx, DATA, OUT);
            RdramGuestAccess mem; mem.Bind(ram);
            mem.Store8(DATA, 28);
            mem.Store32(0x38c740 + 28 * 4, gpu::entries[5]);
            SET_GPR_U32(&ctx, 6, flag);
            ctx.pc = gpu::entries[slot];
            std::vector<uint64_t> actual;
            runtime.eeScheduler().reset(ram, ctx);
            runtime.eeScheduler().scheduleHostCallback(8, [&] {
                actual.push_back(mem.Load32(P));
                actual.push_back(mem.Load32(P + 12));
            });
            runtime.eeScheduler().run();
            Check(actual.size() == 2, "cycle test failed to observe scheduler deadline");
            actual.push_back(completedCycles);
            if (!native) expected = actual;
            else Check(actual == expected, "GPU scheduler cycles/deadline packet state differ");
        }
    }
}
void SnapshotParity(PS2Runtime& runtime) {
    const auto oldPaths = PS2Runtime::getIoPaths();
    auto paths = oldPaths;
    paths.elfDirectory = std::filesystem::current_path() / "gpu-test-config";
    std::filesystem::create_directories(paths.elfDirectory);
    PS2Runtime::setIoPaths(paths);
    std::string expected;
    for (bool native : {false, true}) {
        std::ofstream(paths.elfDirectory / "native_modules.ini") << "gpu=" << (native ? "on" : "off") << '\n';
        RegisterOriginals(runtime);
        ps2_game_overrides::applyMatching(runtime, "AOK.ELF", 0, 0, false);
        auto ram = runtime.memory().getRDRAM();
        R5900Context ctx{};
        Prepare(ram, ctx, DATA, OUT);
        RdramGuestAccess mem; mem.Bind(ram);
        mem.Store32(P + 8, OUT);
        std::ostringstream captured;
        auto previous = std::cerr.rdbuf(captured.rdbuf());
        try {
            ctx.pc = gpu::entries[0];
            RunToReturn(runtime, ctx, runtime.lookupFunction(gpu::entries[0]));
            SET_GPR_U32(&ctx, 4, P); SET_GPR_U32(&ctx, 5, DATA);
            SET_GPR_U32(&ctx, 6, 1); SET_GPR_U32(&ctx, 31, RETURN);
            mem.Store8(DATA, 28); mem.Store32(0x38c740 + 28 * 4, gpu::entries[5]);
            ctx.pc = gpu::entries[6];
            RunToReturn(runtime, ctx, runtime.lookupFunction(gpu::entries[6]));
        } catch (...) { std::cerr.rdbuf(previous); throw; }
        std::cerr.rdbuf(previous);
        std::istringstream lines(captured.str());
        std::string log, line;
        while (std::getline(lines, line))
            if (line.starts_with("[state-snapshot")) log += line + '\n';
        Check(std::count(log.begin(), log.end(), '\n') == 3, "GPU snapshot boundary omitted or duplicated");
        Check(log.find("label=gpu") != std::string::npos && log.find("skip") == std::string::npos,
              "GPU snapshot comparison is empty or skipped");
        if (!native) expected = log;
        else Check(log == expected, "original/native GPU snapshots differ");
    }
    PS2Runtime::setIoPaths(oldPaths);
    std::cout << "3/3 packet/context GPU snapshots match, each boundary captured once\n";
}

}
int main() {
    try {
        setenv("AOE2_STATE_SNAPSHOT", "1", 1);
        PS2Runtime runtime;
        Check(runtime.memory().initialize(), "runtime memory init");
        bool rejected = false;
        try { gpu::Install(runtime); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected && !runtime.hasFunction(0x125a4c), "partial installation silently accepted");
        Differential(runtime);
        HelperDifferential(runtime);
        Continuation(runtime);
        SnapshotParity(runtime);
        SchedulerAndNested(runtime);
        SchedulerCycleParity(runtime);
        std::cout << "GPU differential and runtime continuation tests passed\n";
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n'; return 1;
    }
}
