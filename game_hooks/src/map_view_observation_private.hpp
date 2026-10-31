#pragma once
#include "map_view_module_private.hpp"
#include <aoe2/game_hooks/map_view_config.hpp>
#include "ps2_runtime.h"
#include <mutex>

namespace aoe2::game_hooks::map_view::detail {
constexpr std::array<uint32_t, 16> entries{
    0x31a410, 0x1ab7e0, 0x1abac0, 0x21acd0, 0x1ae350,
    0x2257c0, 0x31a3d0, 0x1ac110, 0x1ac220, 0x25fa10,
    0x25fb10, 0x3209b0, 0x326b80, 0x326f60, 0x31e110,
    0x31b6c0};
struct State {
    MapViewMode mode = MapViewMode::Legacy;
    Relocation relocation;
    MapArenaLease lease;
    std::array<PS2Runtime::RecompiledFunction, entries.size()> originals{};
    std::array<uint32_t, kArenaVSpanSlotCount> vspan_owners{};
    std::array<uint8_t, 1024> last_palette{};
    bool has_last_palette = false;
    uint64_t submissions = 0, palettes = 0, lifecycle = 0;
    std::optional<Snapshot> snapshot;
    // Published by the presentation thread only after a native frame composes.
    std::optional<renderer::MapViewLayout> composed_layout;
};
extern std::mutex mutex;
uint8_t* Bytes(uint8_t*, uint32_t, uint32_t);
uint32_t Word(uint8_t*, uint32_t);
uint16_t Half(uint8_t*, uint32_t);
void WriteWord(uint8_t*, uint32_t, uint32_t);
void Validate(PS2Runtime&, State&);
void ObservePanelInit(uint8_t*, uint32_t);
void Observe(uint8_t*, R5900Context*, PS2Runtime&, State&, size_t);
void PrepareAreaLineTables(uint8_t*, const Relocation&);
void HandleAreaLineTableHook(uint8_t*, R5900Context*, State&, size_t);
bool HandleVSpanInitHook(uint8_t*, R5900Context*, State&);
bool HandleVSpanResetHook(uint8_t*, R5900Context*, State&);
bool HandlePanelRowTableHook(uint8_t*, R5900Context*, State&);
void HandleDrawListAddYHook(R5900Context*, const State&, size_t);
bool HandleHardwareNodeDrawHook(uint8_t*, R5900Context*, PS2Runtime&, State&);
void RecordMapPanelSubmission(uint8_t*, State&);
}
