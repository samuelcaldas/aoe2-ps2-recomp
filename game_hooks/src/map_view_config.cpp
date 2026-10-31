#include <aoe2/game_hooks/map_view_config.hpp>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
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

std::optional<MapViewMode> ReadMapViewConfigFile(std::istream& input) {
    std::string line;
    std::optional<MapViewMode> mode;
    const auto trim = [](std::string value) {
        const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        });
        const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }).base();
        if (first >= last) return std::string{};
        return std::string(first, last);
    };
    while (std::getline(input, line)) {
        const auto content = trim(line);
        if (content.empty() || content.front() == '#') continue;
        if (mode) throw std::invalid_argument("map_view.ini must contain exactly one mode");
        const auto separator = content.find('=');
        if (separator == std::string::npos || trim(content.substr(0, separator)) != "mode")
            throw std::invalid_argument("map_view.ini requires mode=legacy|relocation|native");
        const std::string value = trim(content.substr(separator + 1));
        mode = ReadMapViewConfig(value.c_str(), nullptr);
    }
    if (input.bad()) throw std::invalid_argument("failed reading map_view.ini");
    return mode;
}
}
