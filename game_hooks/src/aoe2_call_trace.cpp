// Diagnostic-build call trace, compiled only when AOE2_RUNTIME_DIAGNOSTICS is ON.
//
// Wraps every guest function listed in the ignored game/SLES-50282/trace_addrs.txt
// (tools/trace_addresses.py) with a logger that calls the original unchanged.
// It answers "which functions of this module does the running game really call,
// with what first argument, and from where" before any of them is replaced.
// A missing list is not an error: nothing is wrapped.
#include "game_overrides.h"
#include "pad_snapshot.hpp"
#include "state_snapshot_capture.hpp"
#include "ps2_runtime.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaxTracedFunctions = 256;
constexpr uint64_t kAlwaysLogged = 3;

struct TracedFunction {
    uint32_t address = 0;
    uint64_t calls = 0;
    PS2Runtime::RecompiledFunction original = nullptr;
};

std::array<TracedFunction, kMaxTracedFunctions> g_traced;

bool should_log(uint64_t call_number)
{
    return call_number <= kAlwaysLogged || (call_number & (call_number - 1)) == 0;
}

template <std::size_t Slot>
void traced_call(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime)
{
    TracedFunction& slot = g_traced[Slot];
    const uint64_t call_number = ++slot.calls;
    if (should_log(call_number)) {
        std::cerr << "[call-trace] addr=0x" << std::hex << slot.address << " n=" << std::dec << call_number
                  << " a0=0x" << std::hex << getRegU32(ctx, 4) << " a1=0x" << getRegU32(ctx, 5)
                  << " ra=0x" << getRegU32(ctx, 31) << std::dec << std::endl;
    }
    const uint32_t slot_address = getRegU32(ctx, 4);
    if (slot.address == 0x001273D8 && !aoe2::game_hooks::diagnostics::native_libpad_installed)
        aoe2::game_hooks::diagnostics::BeginPadStep(rdram, slot_address);
    slot.original(rdram, ctx, runtime);
    if (slot.address == 0x001273D8 && !aoe2::game_hooks::diagnostics::native_libpad_installed)
        aoe2::game_hooks::diagnostics::LogPadStep(rdram, slot_address);
    // Module update boundary for the original code; native handlers call the same function at their own return.
    if (slot.address == 0x00135BF0)
        aoe2::game_hooks::diagnostics::OnModuleUpdate(rdram, aoe2::game_hooks::synthetic::Module::kMouse);
    if (slot.address == 0x00137000)
        aoe2::game_hooks::diagnostics::OnModuleUpdate(rdram, aoe2::game_hooks::synthetic::Module::kKeyboard);
    if (should_log(call_number)) {
        std::cerr << "[call-trace] ret addr=0x" << std::hex << slot.address << " v0=0x"
                  << getRegU32(ctx, 2) << std::dec << std::endl;
    }
}

using Trampoline = PS2Runtime::RecompiledFunction;

template <std::size_t... Slots>
constexpr std::array<Trampoline, sizeof...(Slots)> make_trampolines(std::index_sequence<Slots...>)
{
    return {&traced_call<Slots>...};
}

constexpr auto kTrampolines = make_trampolines(std::make_index_sequence<kMaxTracedFunctions>{});

std::vector<uint32_t> read_trace_list(const std::filesystem::path& path)
{
    std::vector<uint32_t> addresses;
    std::ifstream file(path);
    std::string token;
    while (file >> token)
        addresses.push_back(static_cast<uint32_t>(std::stoul(token, nullptr, 16)));
    return addresses;
}

void install_call_trace(PS2Runtime& runtime)
{
    const auto path = PS2Runtime::getIoPaths().elfDirectory / "trace_addrs.txt";
    const auto addresses = read_trace_list(path);
    std::size_t installed = 0;
    for (const uint32_t address : addresses) {
        if (installed == kMaxTracedFunctions) break;
        const auto original = runtime.lookupFunction(address);
        if (original == nullptr) continue;
        g_traced[installed] = TracedFunction{address, 0, original};
        if (runtime.replaceFunction(address, kTrampolines[installed])) ++installed;
    }
    std::cerr << "[call-trace] wrapped " << installed << "/" << addresses.size() << " functions" << std::endl;
}

} // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 call trace", "AOK.ELF", 0, 0, install_call_trace)
