#pragma once
#include <aoe2/renderer/map_view_layout.hpp>
#include <cstdint>
#include <istream>
#include <optional>
#include <utility>

class PS2Runtime;

namespace aoe2::game_hooks {
enum class MapViewMode { Legacy, Relocation, Native };

/// Parse the opt-in mode; throws std::invalid_argument for invalid or retired overrides.
MapViewMode ReadMapViewConfig(const char* mode, const char* diagnostic_override);

/// Parse the optional one-line map_view.ini mode file.
std::optional<MapViewMode> ReadMapViewConfigFile(std::istream& input);

/// Match the initial presentation size when no explicit window extent is supplied.
std::pair<uint32_t, uint32_t> DefaultMapOutput(bool switch_target, bool docked);

std::optional<renderer::MapViewLayout> ActiveMapViewLayout(PS2Runtime& runtime);
std::optional<renderer::MapViewLayout> ActiveMapViewLayout();
}
