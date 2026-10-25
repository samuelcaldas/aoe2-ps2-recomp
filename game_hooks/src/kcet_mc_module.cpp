#include <aoe2/mc/kt_mc_model.hpp>

#include "game_overrides.h"
#include "guest_rdram_access.hpp"
#include "native_input_arguments.hpp"
#include "native_input_module_support.hpp"
#include "ps2_runtime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace {
using aoe2::game_hooks::native_input::Arguments;
using aoe2::game_hooks::native_input::FunctionEntry;
using aoe2::game_hooks::RdramGuestAccess;
using aoe2::mc::KtMcModel;

struct NativeMc {
    explicit NativeMc() : model(memory) {}
    RdramGuestAccess memory;
    KtMcModel model;
};

std::unique_ptr<NativeMc> g_mc;

enum Slot : std::size_t {
#define ENTRY_R(name, address, expression) kSlot_##name,
#define ENTRY_V(name, address, statement) kSlot_##name,
#define DEFERRED(address, reason)
#include "kt_mc_entries.inc"
#undef ENTRY_R
#undef ENTRY_V
#undef DEFERRED
    kSlotCount
};

std::array<std::atomic<uint64_t>, kSlotCount> g_call_counts{};

bool ShouldLog(uint64_t call_number) {
    return call_number <= 3 || (call_number & (call_number - 1)) == 0;
}

template <typename Body>
void Run(std::size_t slot, const char* name, uint32_t address, const Arguments& arguments,
         uint8_t* rdram, R5900Context* context, bool returns_value, Body body) {
    const uint64_t call_number = ++g_call_counts[slot];
    if (ShouldLog(call_number)) {
        std::cerr << "[native-mc] " << name << " n=" << call_number;
        for (std::size_t index = 0; index < 3; ++index)
            std::cerr << " a" << index << "=0x" << std::hex << arguments.U(index);
        std::cerr << std::dec << '\n';
    }
    g_mc->memory.Bind(rdram);
    const uint32_t result = body(g_mc->model);
    if (returns_value) setReturnU32(context, result);
    context->pc = getRegU32(context, 31);
}

#define I(index) arguments.I(index)
#define U(index) arguments.U(index)
#define ENTRY_R(name, address, expression) \
    void Handler_##name(uint8_t* rdram, R5900Context* context, PS2Runtime*) { \
        const auto arguments = Arguments::FromRegisters(aoe2::game_hooks::native_input::ReadRegisters(context)); \
        Run(kSlot_##name, #name, address, arguments, rdram, context, true, \
            [&](KtMcModel& m) -> uint32_t { return static_cast<uint32_t>(expression); }); \
    }
#define ENTRY_V(name, address, statement) \
    void Handler_##name(uint8_t* rdram, R5900Context* context, PS2Runtime*) { \
        const auto arguments = Arguments::FromRegisters(aoe2::game_hooks::native_input::ReadRegisters(context)); \
        Run(kSlot_##name, #name, address, arguments, rdram, context, false, \
            [&](KtMcModel& m) -> uint32_t { statement; return 0; }); \
    }
#define DEFERRED(address, reason)
#include "kt_mc_entries.inc"
#undef ENTRY_R
#undef ENTRY_V
#undef DEFERRED
#undef I
#undef U

const std::array<FunctionEntry, kSlotCount> kEntries = {{
#define ENTRY_R(name, address, expression) {address, &Handler_##name, #name},
#define ENTRY_V(name, address, statement) {address, &Handler_##name, #name},
#define DEFERRED(address, reason)
#include "kt_mc_entries.inc"
#undef ENTRY_R
#undef ENTRY_V
#undef DEFERRED
}};

void InstallNativeMc(PS2Runtime& runtime) {
    const auto switches = aoe2::game_hooks::native_input::ReadSwitches();
    if (!switches.Enabled("memory_card")) {
        std::cout << "[aoe2] native kt_mc_2: off\n";
        return;
    }
    g_mc = std::make_unique<NativeMc>();
    std::size_t installed = 0;
    for (const auto& entry : kEntries) {
        if (!runtime.hasFunction(entry.address)) continue;
        if (runtime.replaceFunction(entry.address, entry.handler)) ++installed;
    }
    std::cout << "[aoe2] native kt_mc_2: installed " << installed << "/" << kEntries.size() << " functions\n";
}

}  // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native memory card module", "AOK.ELF", 0, 0, InstallNativeMc)
