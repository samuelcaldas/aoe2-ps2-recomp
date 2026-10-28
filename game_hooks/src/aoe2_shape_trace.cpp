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
constexpr int kRegA3 = 7;
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

// First sight of each shape object: its 0x14 bytes and, for each word that points into RAM,
// the first bytes there (looking for the SLP "2.0N" header).
void DumpShapeOnce(uint8_t* rdram, uint32_t shape) {
    static std::vector<uint32_t> seen;
    if (std::find(seen.begin(), seen.end(), shape) != seen.end() || seen.size() >= 400) return;
    seen.push_back(shape);
    std::cout << "[shape-trace] shape=0x" << std::hex << shape << " name=" << g_model.NameOf(shape);
    for (uint32_t offset = 0; offset < 0x14; offset += 4) {
        uint32_t word = 0;
        if (const uint8_t* source = getConstMemPtr(rdram, shape + offset)) std::memcpy(&word, source, sizeof(word));
        std::cout << " +" << offset << "=" << word;
        const uint8_t* target = (word > 0x100000 && word < 0x2000000) ? getConstMemPtr(rdram, word) : nullptr;
        if (target != nullptr && offset == 8) {
            std::cout << "[";
            for (int i = 0; i < 160; ++i) std::cout << (i ? " " : "") << static_cast<uint32_t>(target[i]);
            std::cout << "]";
        }
    }
    std::cout << std::dec << std::endl;
}

// AOE2_SHAPE_TRACE_DRAWS=<n>: the first n draws' arguments (shape, area, x, y, frame, flags).
void LogDrawSample(R5900Context* ctx) {
    static const uint64_t limit = [] {
        const char* text = std::getenv("AOE2_SHAPE_TRACE_DRAWS");
        return text != nullptr ? std::strtoull(text, nullptr, 10) : uint64_t{0};
    }();
    static uint64_t logged = 0;
    if (logged >= limit) return;
    ++logged;
    std::cout << "[shape-draw] shape=0x" << std::hex << getRegU32(ctx, kRegA0) << " area=0x" << getRegU32(ctx, kRegA1)
              << std::dec << " x=" << static_cast<int32_t>(getRegU32(ctx, kRegA2))
              << " y=" << static_cast<int32_t>(getRegU32(ctx, kRegA3)) << " frame=" << static_cast<int32_t>(getRegU32(ctx, kRegT0))
              << " flags=" << static_cast<int32_t>(getRegU32(ctx, kRegT0 + 1)) << std::endl;
}

// First sight of each draw area: its leading words (buffer, size, clip fields) for layout discovery.
void DumpAreaOnce(uint8_t* rdram, uint32_t area) {
    static std::vector<uint32_t> seen;
    if (std::find(seen.begin(), seen.end(), area) != seen.end() || seen.size() >= 32) return;
    seen.push_back(area);
    std::cout << "[shape-trace] area=0x" << std::hex << area;
    for (uint32_t offset = 0; offset < 0x80; offset += 4) {
        uint32_t word = 0;
        if (const uint8_t* source = getConstMemPtr(rdram, area + offset)) std::memcpy(&word, source, sizeof(word));
        std::cout << " +" << offset << "=" << word;
    }
    std::cout << std::dec << std::endl;
}

template <std::size_t Slot>
void ObservedDraw(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_model.Drawn(getRegU32(ctx, kRegA0), static_cast<int32_t>(getRegU32(ctx, kRegT0)));
        DumpAreaOnce(rdram, getRegU32(ctx, kRegA1));
        DumpShapeOnce(rdram, getRegU32(ctx, kRegA0));
        LogDrawSample(ctx);
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

// AOE2_MAP_VIEW_OVERRIDE=<w>x<h>: experiment. The map view panel initializer (guest 0x31a410, $a0 =
// panel, +0x14 width, +0x18 height) runs with that size to learn whether view size is data-driven.
constexpr uint32_t kMapViewInit = 0x0031A410;
constexpr uint32_t kPanelWidth = 0x14;
constexpr uint32_t kPanelHeight = 0x18;
int32_t g_view_width = 0;
int32_t g_view_height = 0;
PS2Runtime::RecompiledFunction g_original_view_init = nullptr;

void ObservedViewInit(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t panel = getRegU32(ctx, kRegA0);
    auto* width = reinterpret_cast<int32_t*>(getMemPtr(rdram, panel + kPanelWidth));
    auto* height = reinterpret_cast<int32_t*>(getMemPtr(rdram, panel + kPanelHeight));
    if (width != nullptr && height != nullptr) {
        std::cout << "[map-view] panel=0x" << std::hex << panel << std::dec << " size " << *width << "x" << *height
                  << " -> " << g_view_width << "x" << g_view_height << std::endl;
        *width = g_view_width;
        *height = g_view_height;
    }
    g_original_view_init(rdram, ctx, runtime);
}

void InstallMapViewOverride(PS2Runtime& runtime) {
    const char* text = std::getenv("AOE2_MAP_VIEW_OVERRIDE");
    if (text == nullptr) return;
    char* separator = nullptr;
    g_view_width = static_cast<int32_t>(std::strtol(text, &separator, 10));
    g_view_height = separator != nullptr && *separator == 'x' ? static_cast<int32_t>(std::strtol(separator + 1, nullptr, 10)) : 0;
    if (g_view_width <= 0 || g_view_height <= 0) throw std::invalid_argument("AOE2_MAP_VIEW_OVERRIDE must be <w>x<h>");
    Wrap(runtime, kMapViewInit, ObservedViewInit, g_original_view_init);
    std::cout << "[map-view] override " << g_view_width << "x" << g_view_height << std::endl;
}

void InstallShapeTrace(PS2Runtime& runtime) {
    InstallMapViewOverride(runtime);
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
