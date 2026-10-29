#include <aoe2/renderer/map_view_layout.hpp>
#include <stdexcept>

namespace aoe2::renderer {
namespace {
HudLayout CompleteHudBands(aoe2::input::Extent output, float scale) {
    const float left = (output.width - 640.0f * scale) / 2.0f;
    return HudLayout::ForExtent({640, 448}, output, {
        {HudRegionRole::ResourceBar, HudAnchor::Top, {0, 0, 640, 40}, 640 * scale, 40 * scale, left, 0},
        {HudRegionRole::CommandBar, HudAnchor::Bottom, {0, 302, 640, 146}, 640 * scale, 146 * scale, left, 0},
    });
}
}
MapViewLayout MapViewLayout::ForOutput(uint32_t width, uint32_t height) {
    if (width < 640 || width > 1920 || height < 448 || height > 1080)
        throw std::invalid_argument("native map output must fit 640x448 through 1920x1080");
    const float scale = height == 1080 && width >= 1280 ? 2.0f : 1.0f;
    const aoe2::input::Extent output{static_cast<float>(width), static_cast<float>(height)};
    const Rect map{0, 40 * scale, output.width, output.height - 186 * scale};
    return {output, map, CompleteHudBands(output, scale)};
}
}
