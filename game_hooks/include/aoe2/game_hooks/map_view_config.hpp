#pragma once
#include <aoe2/renderer/map_view_layout.hpp>
#include <optional>

class PS2Runtime;

namespace aoe2::game_hooks {
enum class MapViewMode { Legacy, Native };

/// Parse the opt-in mode; throws std::invalid_argument for invalid or retired overrides.
MapViewMode ReadMapViewConfig(const char* mode, const char* diagnostic_override);

std::optional<renderer::MapViewLayout> ActiveMapViewLayout(PS2Runtime& runtime);
std::optional<renderer::MapViewLayout> ActiveMapViewLayout();
}
