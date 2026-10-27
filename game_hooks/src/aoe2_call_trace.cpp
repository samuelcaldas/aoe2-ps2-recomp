// Diagnostic-build call trace, compiled only when AOE2_RUNTIME_DIAGNOSTICS is ON.
//
// Wraps every guest function listed in the ignored game/SLES-50282/trace_addrs.txt
// (tools/trace_addresses.py) with a logger that calls the original unchanged.
// It answers "which functions of this module does the running game really call,
// with what first argument, and from where" before any of them is replaced.
// A missing list is not an error: nothing is wrapped.
#include "game_overrides.h"
#include <aoe2/game_hooks/native_input.hpp>
#include "file_snapshot.hpp"
#include "hd_snapshot.hpp"
#include "mc_snapshot.hpp"
#include "pad_snapshot.hpp"
#include "state_snapshot_capture.hpp"
#include "ps2_runtime.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t kMaxTracedFunctions = 256;
// AOE2_CALL_TRACE_FIRST raises how many leading calls per function are always logged (default 3).
uint64_t AlwaysLogged() {
    static const uint64_t value = [] {
        const char* text = std::getenv("AOE2_CALL_TRACE_FIRST");
        return text != nullptr ? std::strtoull(text, nullptr, 10) : uint64_t{3};
    }();
    return value;
}

struct TracedFunction {
    uint32_t address = 0;
    uint64_t calls = 0;
    PS2Runtime::RecompiledFunction original = nullptr;
};

std::array<TracedFunction, kMaxTracedFunctions> g_traced;

bool should_log(uint64_t call_number)
{
    return call_number <= AlwaysLogged() || (call_number & (call_number - 1)) == 0;
}

// AOE2_CALL_TRACE_MEM=1 also prints the four words a0 points at (a message or argument block).
void PrintArgumentMemory(const uint8_t* rdram, uint32_t address) {
    static const bool enabled = std::getenv("AOE2_CALL_TRACE_MEM") != nullptr;
    constexpr uint32_t kRamBytes = 32u * 1024u * 1024u;
    if (!enabled || address == 0 || address > kRamBytes - 16u || (address & 3u) != 0) return;
    std::cerr << " [a0]={" << std::hex;
    for (uint32_t offset = 0; offset < 16; offset += 4) {
        uint32_t word = 0;
        std::memcpy(&word, rdram + address + offset, sizeof(word));
        std::cerr << (offset ? "," : "") << "0x" << word;
    }
    std::cerr << "}" << std::dec;
}

template <std::size_t Slot>
void traced_call(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime)
{
    TracedFunction& slot = g_traced[Slot];
    const uint64_t call_number = ++slot.calls;
    if (should_log(call_number)) {
        std::cerr << "[call-trace] addr=0x" << std::hex << slot.address << " n=" << std::dec << call_number
                  << " a0=0x" << std::hex << getRegU32(ctx, 4) << " a1=0x" << getRegU32(ctx, 5) << " a2=0x" << getRegU32(ctx, 6) << " a3=0x" << getRegU32(ctx, 7)
                  << " ra=0x" << getRegU32(ctx, 31) << std::dec;
        PrintArgumentMemory(rdram, getRegU32(ctx, 4));
        std::cerr << std::endl;
    }
    const uint32_t slot_address = getRegU32(ctx, 4);
    if (slot.address == 0x001273D8 && !aoe2::game_hooks::diagnostics::native_libpad_installed)
        aoe2::game_hooks::diagnostics::BeginPadStep(rdram, slot_address);
    slot.original(rdram, ctx, runtime);
    if (slot.address == 0x001273D8 && !aoe2::game_hooks::diagnostics::native_libpad_installed)
        aoe2::game_hooks::diagnostics::LogPadStep(rdram, slot_address);
    // Module update boundary for the original code; native handlers call the same function at their own return.
    if (slot.address == 0x00135BF0 && !aoe2::game_hooks::NativeMouseModuleInstalled())
        aoe2::game_hooks::diagnostics::OnModuleUpdate(rdram, aoe2::game_hooks::synthetic::Module::kMouse);
    if (slot.address == 0x00137000 && !aoe2::game_hooks::NativeKeyboardModuleInstalled())
        aoe2::game_hooks::diagnostics::OnModuleUpdate(rdram, aoe2::game_hooks::synthetic::Module::kKeyboard);
    if (slot.address == 0x0012D540 || slot.address == 0x0012CCE8)
        aoe2::game_hooks::diagnostics::ObserveFileState(rdram, call_number);
    if (slot.address == 0x00129B10)
        aoe2::game_hooks::diagnostics::ObserveCdvdState(rdram, call_number);
    if (slot.address == 0x0012DE88 || slot.address == 0x0012E068)
        aoe2::game_hooks::diagnostics::ObserveMcState(rdram, call_number);
    if (slot.address == 0x00128BA8 || slot.address == 0x00129588)
        aoe2::game_hooks::diagnostics::ObserveHdState(rdram, call_number);
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
