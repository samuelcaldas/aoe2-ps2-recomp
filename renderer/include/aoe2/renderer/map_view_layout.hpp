#pragma once
#include <aoe2/renderer/hud_layout.hpp>
#include <cstdint>

namespace aoe2::renderer {
struct MapViewLayout {
    aoe2::input::Extent output;
    Rect map;
    HudLayout hud;

    /// Keep complete logical HUD bands at the top/bottom; throws for unsupported output bounds.
    static MapViewLayout ForOutput(uint32_t width, uint32_t height);
};
}
