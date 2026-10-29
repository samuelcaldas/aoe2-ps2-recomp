#pragma once
#include <aoe2/renderer/map_view_layout.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace aoe2::renderer {
using MapPalette = std::array<std::array<uint8_t, 4>, 256>;
struct RgbaFrameView {
    std::span<const uint8_t> pixels;
    uint32_t width, height, strideBytes;
};
struct MapFrameSnapshot {
    uint32_t width, height, pitch;
    std::vector<uint8_t> indices;
    MapPalette palette;
    MapViewLayout layout;
};
struct ComposedMapFrame {
    std::shared_ptr<const std::vector<uint8_t>> pixels;
    uint32_t width, height, strideBytes;
};
class MapViewCompositor {
public:
    /// Compose immutable effective-RGBA palette pixels and complete legacy HUD bands; throws for invalid bounds.
    static ComposedMapFrame Compose(RgbaFrameView legacy, const MapFrameSnapshot& snapshot);
};
}
