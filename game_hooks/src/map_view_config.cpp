#include <aoe2/game_hooks/map_view_config.hpp>
#include <stdexcept>
#include <string_view>

namespace aoe2::game_hooks {
std::pair<uint32_t, uint32_t> DefaultMapOutput(bool switch_target, bool docked) {
    if (!switch_target) return {640, 448};
    return docked ? std::pair<uint32_t, uint32_t>{1920, 1080}
                  : std::pair<uint32_t, uint32_t>{1280, 720};
}
MapViewMode ReadMapViewConfig(const char* mode, const char* diagnostic_override) {
    if (diagnostic_override && *diagnostic_override)
        throw std::invalid_argument("AOE2_MAP_VIEW_OVERRIDE is retired; use AOE2_MAP_VIEW=legacy|relocation|native");
    if (!mode) return MapViewMode::Legacy;
    const std::string_view value(mode);
    if (value == "legacy") return MapViewMode::Legacy;
    if (value == "relocation") return MapViewMode::Relocation;
    if (value == "native") return MapViewMode::Native;
    throw std::invalid_argument("AOE2_MAP_VIEW must be legacy, relocation, or native");
}
}
