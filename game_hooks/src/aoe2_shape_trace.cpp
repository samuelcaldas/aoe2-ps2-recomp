// Diagnostic-build shape-draw trace, enabled with AOE2_SHAPE_TRACE=<draws per report>.
//
// Wraps the game's SLP shape loader and shape-draw entry points with observers that
// call the originals unchanged, and reports which shapes (loader name and resource
// id) and frames are drawn. Entry points and argument registers follow the authorized
// decomp (../aoe, commit c3233fd4f): func_0025D6D0(shape, name, resource_id) in
// src/render/render_manager_surfaces.cpp, and func_0025E640(shape, area, x, y, frame,
// flags) in src/render/render_helper_2184D0.cpp. AOE2_SHAPE_TRACE_EXTRA adds draw entry
// points with the same argument layout (comma-separated guest addresses).
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "shape_trace_model.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kShapeLoad = 0x0025D6D0;
constexpr uint32_t kShapeDraw = 0x0025E640;
constexpr std::size_t kMaxDrawEntries = 4;
constexpr std::size_t kReportRows = 48;
constexpr int kRegA0 = 4;
constexpr int kRegA1 = 5;
constexpr int kRegA2 = 6;
constexpr int kRegT0 = 8;

std::mutex g_mutex;
aoe2::game_hooks::ShapeTraceModel g_model;
uint64_t g_interval = 0;
PS2Runtime::RecompiledFunction g_original_load = nullptr;
std::array<PS2Runtime::RecompiledFunction, kMaxDrawEntries> g_original_draws{};

std::string GuestString(uint8_t* rdram, uint32_t address) {
    const char* text = reinterpret_cast<const char*>(getConstMemPtr(rdram, address));
    if (text == nullptr) return "?";
    return std::string(text, strnlen(text, 63));
}

void ObservedLoad(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_model.Loaded(getRegU32(ctx, kRegA0), GuestString(rdram, getRegU32(ctx, kRegA1)),
                       static_cast<int32_t>(getRegU32(ctx, kRegA2)));
    }
    g_original_load(rdram, ctx, runtime);
}

template <std::size_t Slot>
void ObservedDraw(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_model.Drawn(getRegU32(ctx, kRegA0), static_cast<int32_t>(getRegU32(ctx, kRegT0)));
        if (g_model.draws() % g_interval == 0)
            std::cout << "[shape-trace] draws=" << g_model.draws() << " entry=" << Slot << " "
                      << g_model.Report(kReportRows) << std::endl;
    }
    g_original_draws[Slot](rdram, ctx, runtime);
}

constexpr std::array<PS2Runtime::RecompiledFunction, kMaxDrawEntries> kDrawObservers{
    ObservedDraw<0>, ObservedDraw<1>, ObservedDraw<2>, ObservedDraw<3>};

std::vector<uint32_t> DrawEntries() {
    std::vector<uint32_t> entries{kShapeDraw};
    const char* extra = std::getenv("AOE2_SHAPE_TRACE_EXTRA");
    for (char* cursor = const_cast<char*>(extra); cursor != nullptr && *cursor != '\0';) {
        char* end = nullptr;
        entries.push_back(static_cast<uint32_t>(std::strtoul(cursor, &end, 0)));
        cursor = (end != nullptr && *end == ',') ? end + 1 : nullptr;
    }
    if (entries.size() > kMaxDrawEntries) throw std::invalid_argument("AOE2_SHAPE_TRACE_EXTRA: too many entries");
    return entries;
}

void Wrap(PS2Runtime& runtime, uint32_t address, PS2Runtime::RecompiledFunction observer,
          PS2Runtime::RecompiledFunction& original) {
    original = runtime.lookupFunction(address);
    if (!original || !runtime.replaceFunction(address, observer))
        throw std::runtime_error("shape trace: cannot wrap guest function");
}

void InstallShapeTrace(PS2Runtime& runtime) {
    const char* text = std::getenv("AOE2_SHAPE_TRACE");
    if (text == nullptr) return;
    g_interval = std::max<uint64_t>(1, std::strtoull(text, nullptr, 10));
    Wrap(runtime, kShapeLoad, ObservedLoad, g_original_load);
    const std::vector<uint32_t> entries = DrawEntries();
    for (std::size_t slot = 0; slot < entries.size(); ++slot)
        Wrap(runtime, entries[slot], kDrawObservers[slot], g_original_draws[slot]);
    std::cout << "[shape-trace] wrapped loader and " << entries.size() << " draw entry point(s)\n";
}

} // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 shape trace", "AOK.ELF", 0, 0, InstallShapeTrace)
