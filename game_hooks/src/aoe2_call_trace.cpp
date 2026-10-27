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
#include <string>
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

// AOE2_GS_DUMP_CALL=N: on the N-th call of a traced function, write the display buffers and the
// font texture/CLUT the boot screens use to build/desktop-validation/gs-*.ppm, with nonzero counts.
// Bases are GS block addresses (TBP/CBP units); frame bases are FBP * 32.
void WriteGsPlane(PS2Runtime* runtime, const char* name, uint32_t psm, uint32_t base, uint32_t bw,
                  uint32_t width, uint32_t height) {
    std::ofstream out(std::string("build/desktop-validation/gs-") + name + ".ppm", std::ios::binary);
    out << "P6\n" << width << ' ' << height << "\n255\n";
    uint64_t nonzero = 0;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const uint32_t value = runtime->gs().ReadVram(psm, base, bw, x, y);
            nonzero += value != 0;
            uint8_t rgb[3];
            if (psm == 0x02) {  // CT16: 5-5-5
                rgb[0] = uint8_t((value & 0x1F) << 3); rgb[1] = uint8_t(((value >> 5) & 0x1F) << 3);
                rgb[2] = uint8_t(((value >> 10) & 0x1F) << 3);
            } else if (psm == 0x14 || psm == 0x13) {  // indices as grey
                const uint8_t grey = uint8_t(psm == 0x14 ? value * 17 : value);
                rgb[0] = rgb[1] = rgb[2] = grey;
            } else {
                rgb[0] = uint8_t(value); rgb[1] = uint8_t(value >> 8); rgb[2] = uint8_t(value >> 16);
            }
            out.write(reinterpret_cast<const char*>(rgb), 3);
        }
    }
    std::cerr << "[gs-dump] " << name << " psm=0x" << std::hex << psm << std::dec << " base=" << base
              << " nonzero=" << nonzero << "/" << uint64_t(width) * height << std::endl;
}

void MaybeDumpGs(PS2Runtime* runtime, uint64_t call_number) {
    static const uint64_t at = [] {
        const char* text = std::getenv("AOE2_GS_DUMP_CALL");
        return text != nullptr ? std::strtoull(text, nullptr, 10) : uint64_t{0};
    }();
    if (at == 0 || call_number != at) return;
    WriteGsPlane(runtime, "fb0", 0x02, 0 * 32, 10, 640, 448);
    WriteGsPlane(runtime, "fb80", 0x02, 80 * 32, 10, 640, 448);
    WriteGsPlane(runtime, "fb244", 0x02, 244 * 32, 10, 640, 448);
    WriteGsPlane(runtime, "fb256", 0x02, 256 * 32, 10, 640, 448);
    WriteGsPlane(runtime, "font-t4", 0x14, 10432, 16, 1024, 256);
    WriteGsPlane(runtime, "image-t8", 0x13, 7680, 2, 128, 128);
    WriteGsPlane(runtime, "back-t8", 0x13, 10432, 10, 640, 448);
    WriteGsPlane(runtime, "clut-8060", 0x00, 8060, 1, 16, 16);
    WriteGsPlane(runtime, "clut-8000", 0x00, 8000, 1, 16, 16);
    std::cerr << "[gs-dump] samples fb0 center=0x" << std::hex << runtime->gs().ReadVram(0x02, 0, 10, 320, 224)
              << " fb80 center=0x" << runtime->gs().ReadVram(0x02, 80 * 32, 10, 320, 224) << " clut8000:";
    for (uint32_t i = 0; i < 8; ++i) std::cerr << " 0x" << runtime->gs().ReadVram(0x00, 8000, 1, i, 0);
    std::cerr << " clut7944:";
    for (uint32_t i = 0; i < 8; ++i) std::cerr << " 0x" << runtime->gs().ReadVram(0x00, 7944, 1, i, 0);
    std::cerr << " clut8060:";
    for (uint32_t i = 0; i < 8; ++i) std::cerr << " 0x" << runtime->gs().ReadVram(0x00, 8060, 1, i, 0);
    std::cerr << std::dec << std::endl;
}

template <std::size_t Slot>
void traced_call(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime)
{
    TracedFunction& slot = g_traced[Slot];
    const uint64_t call_number = ++slot.calls;
    if (Slot == 0) MaybeDumpGs(runtime, call_number);
    if (should_log(call_number)) {
        std::cerr << "[call-trace] addr=0x" << std::hex << slot.address << " n=" << std::dec << call_number
                  << " a0=0x" << std::hex << getRegU32(ctx, 4) << " a1=0x" << getRegU32(ctx, 5) << " a2=0x" << getRegU32(ctx, 6) << " a3=0x" << getRegU32(ctx, 7)
                  << " ra=0x" << getRegU32(ctx, 31) << " sp=0x" << getRegU32(ctx, 29) << std::dec;
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
                  << getRegU32(ctx, 2) << " sp=0x" << getRegU32(ctx, 29) << " ra=0x" << getRegU32(ctx, 31)
                  << " pc=0x" << ctx->pc << std::dec << std::endl;
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
