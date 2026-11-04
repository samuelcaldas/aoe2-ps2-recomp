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
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
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
PS2Runtime::RecompiledFunction g_original_raster = nullptr;
bool g_capture_minimap = false;
std::array<PS2Runtime::RecompiledFunction, kMaxDrawEntries> g_original_draws{};

std::string GuestString(uint8_t* rdram, uint32_t address) {
    const char* text = reinterpret_cast<const char*>(getConstMemPtr(rdram, address));
    if (text == nullptr) return "?";
    return std::string(text, strnlen(text, 63));
}

uint32_t ReadWord(uint8_t* rdram, uint32_t address) {
    uint32_t value = 0;
    if (const uint8_t* source = getConstMemPtr(rdram, address))
        std::memcpy(&value, source, sizeof(value));
    return value;
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

// AOE2_SHAPE_TRACE_PIXELS=<prefix> writes the affected CT16 draw area before and
// after its first n_icomap.slp#56260 draw. This keeps pixel contents out of the
// normal log while allowing the rasterizer output to be inspected directly.
void DumpMinimapArea(uint8_t* rdram, uint32_t area, const char* phase) {
    static bool captured = false;
    const char* prefix = std::getenv("AOE2_SHAPE_TRACE_PIXELS");
    if (captured || prefix == nullptr || *prefix == '\0') return;
    const uint32_t buffer = ReadWord(rdram, area + 0xcc);
    const uint32_t width = ReadWord(rdram, area + 0x18);
    const uint32_t height = ReadWord(rdram, area + 0x1c);
    const uint32_t pitch = ReadWord(rdram, area + 0x20);
    if (buffer == 0 || width == 0 || height == 0 || width > 2048 || height > 2048 || pitch < width * 2) return;

    std::ofstream image(std::string(prefix) + "_" + phase + ".ppm", std::ios::binary);
    if (!image) return;
    image << "P6\n" << width << " " << height << "\n255\n";
    const uint8_t* pixels = getConstMemPtr(rdram, buffer);
    if (pixels == nullptr) return;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            uint16_t pixel = 0;
            std::memcpy(&pixel, pixels + y * pitch + x * 2, sizeof(pixel));
            const uint8_t rgb[3]{
                static_cast<uint8_t>((pixel & 0x1f) * 255 / 31),
                static_cast<uint8_t>(((pixel >> 5) & 0x1f) * 255 / 31),
                static_cast<uint8_t>(((pixel >> 10) & 0x1f) * 255 / 31)};
            image.write(reinterpret_cast<const char*>(rgb), sizeof(rgb));
        }
    }
    if (std::string_view(phase) == "after") {
        captured = true;
        std::cout << "[shape-trace] CT16 minimap area snapshots: " << prefix << "_{before,after}.ppm\n";
    }
}

void ObservedRaster(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static bool dumped = false;
    if (g_capture_minimap && !dumped) {
        const std::string prefix = std::getenv("AOE2_SHAPE_TRACE_PIXELS");
        std::ofstream ram(prefix + "_raster_ram.bin", std::ios::binary);
        ram.write(reinterpret_cast<const char*>(rdram), 32 * 1024 * 1024);
        std::ofstream context(prefix + "_raster_ctx.bin", std::ios::binary);
        context.write(reinterpret_cast<const char*>(ctx), sizeof(*ctx));
        dumped = true;
        std::cout << "[shape-trace] captured rasterizer entry" << std::endl;
    }
    g_original_raster(rdram, ctx, runtime);
}

template <std::size_t Slot>
void ObservedDraw(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    const uint32_t shape = getRegU32(ctx, kRegA0);
    const uint32_t area = getRegU32(ctx, kRegA1);
    bool captureMinimap = false;
    if (std::getenv("AOE2_SHAPE_TRACE_PIXELS") != nullptr) {
        std::lock_guard<std::mutex> lock(g_mutex);
        captureMinimap = g_model.NameOf(shape).find("n_icomap.slp#56260") != std::string::npos;
    }
    if (captureMinimap) {
        static bool dumped = false;
        if (!dumped) {
            const std::string prefix = std::getenv("AOE2_SHAPE_TRACE_PIXELS");
            std::ofstream ram(prefix + "_ram.bin", std::ios::binary);
            ram.write(reinterpret_cast<const char*>(rdram), 32 * 1024 * 1024);
            std::ofstream vram(prefix + "_vram.bin", std::ios::binary);
            vram.write(reinterpret_cast<const char*>(runtime->memory().getGSVRAM()), 4 * 1024 * 1024);
            std::ofstream context(prefix + "_ctx.bin", std::ios::binary);
            context.write(reinterpret_cast<const char*>(ctx), sizeof(*ctx));
            std::cout << "[shape-trace] snapshot shape=0x" << std::hex << shape
                      << " area=0x" << area << " pixels=0x" << ReadWord(rdram, area + 0xcc)
                      << std::dec << std::endl;
            dumped = true;
        }
        DumpMinimapArea(rdram, area, "before");
    }
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
    static int pendingHistory = -1;
    if (captureMinimap && pendingHistory < 0) pendingHistory = 0;
    if (pendingHistory >= 0 && ++pendingHistory == 100) {
        const std::string prefix = std::getenv("AOE2_SHAPE_TRACE_PIXELS");
        std::ofstream vram(prefix + "_later_vram.bin", std::ios::binary);
        vram.write(reinterpret_cast<const char*>(runtime->memory().getGSVRAM()), 4 * 1024 * 1024);
        const auto history = runtime->gs().getDebugHistory();
        std::ofstream hist(prefix + "_history.bin", std::ios::binary);
        hist.write(reinterpret_cast<const char*>(history.data()), history.size() * sizeof(history[0]));
        std::cout << "[shape-trace] GS history entries=" << history.size()
                  << " record bytes=" << sizeof(history[0]) << std::endl;
    }
    g_capture_minimap = captureMinimap;
    g_original_draws[Slot](rdram, ctx, runtime);
    g_capture_minimap = false;
    if (captureMinimap) DumpMinimapArea(rdram, area, "after");
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
    if (std::getenv("AOE2_SHAPE_TRACE_PIXELS"))
        Wrap(runtime, 0x17e350, ObservedRaster, g_original_raster);
    const std::vector<uint32_t> entries = DrawEntries();
    for (std::size_t slot = 0; slot < entries.size(); ++slot)
        Wrap(runtime, entries[slot], kDrawObservers[slot], g_original_draws[slot]);
    std::cout << "[shape-trace] wrapped loader and " << entries.size() << " draw entry point(s)\n";
}

} // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 shape trace", "AOK.ELF", 0, 0, InstallShapeTrace)
