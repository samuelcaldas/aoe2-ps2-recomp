#pragma once
#include "map_view_module_private.hpp"
#include <aoe2/game_hooks/map_view_config.hpp>
#include "ps2_runtime.h"
#include <mutex>

namespace aoe2::game_hooks::map_view::detail {
constexpr std::array<uint32_t, 7> entries{0x31a410, 0x1ab7e0, 0x1abac0, 0x21acd0, 0x1ae350, 0x2257c0, 0x31a3d0};
struct State {
    MapViewMode mode = MapViewMode::Legacy;
    Relocation relocation;
    MapArenaLease lease;
    std::array<PS2Runtime::RecompiledFunction, entries.size()> originals{};
    uint64_t submissions = 0, palettes = 0, lifecycle = 0;
    std::optional<Snapshot> snapshot;
    // Published by the presentation thread only after a native frame composes.
    std::optional<renderer::MapViewLayout> composed_layout;
};
extern std::mutex mutex;
uint8_t* Bytes(uint8_t*, uint32_t, uint32_t);
uint32_t Word(uint8_t*, uint32_t);
void Validate(PS2Runtime&, State&);
void ObservePanelInit(uint8_t*, uint32_t);
void Observe(uint8_t*, R5900Context*, PS2Runtime&, State&, size_t);
}
