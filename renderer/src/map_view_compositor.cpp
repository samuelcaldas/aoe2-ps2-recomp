#include <aoe2/renderer/map_view_compositor.hpp>
#include <aoe2/input/presentation_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace aoe2::renderer {
namespace {
void ValidateLegacy(RgbaFrameView frame) {
    if (frame.width == 0 || frame.height == 0 || frame.width > 1920 || frame.height > 1080)
        throw std::invalid_argument("legacy frame dimensions are invalid");
    if (frame.strideBytes < uint64_t{frame.width} * 4 || frame.pixels.size() < uint64_t{frame.strideBytes} * frame.height)
        throw std::invalid_argument("legacy RGBA frame storage is truncated");
}
uint32_t CheckedExtent(float value, uint32_t minimum, uint32_t maximum) {
    if (!std::isfinite(value) || value < minimum || value > maximum || std::floor(value) != value)
        throw std::invalid_argument("composed frame extent must be a bounded integer");
    return static_cast<uint32_t>(value);
}
bool SameRectangle(Rect first, Rect second) {
    return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
}
void ValidateHud(const HudLayout& actual, const HudLayout& expected) {
    if (actual.regions().size() != expected.regions().size())
        throw std::invalid_argument("HUD layout and map snapshot disagree");
    for (std::size_t index = 0; index < actual.regions().size(); ++index) {
        const auto& region = actual.regions()[index];
        const auto& reference = expected.regions()[index];
        if (region.role != reference.role || !SameRectangle(region.guest_source, reference.guest_source) ||
            !SameRectangle(region.destination, reference.destination))
            throw std::invalid_argument("HUD layout and map snapshot disagree");
    }
}
void ValidateMap(const MapFrameSnapshot& snapshot) {
    const auto canonical = MapViewLayout::ForOutput(CheckedExtent(snapshot.layout.output.width, 640, 1920),
                                                   CheckedExtent(snapshot.layout.output.height, 448, 1080));
    if (snapshot.width != canonical.map.width || snapshot.height != canonical.map.height ||
        !SameRectangle(snapshot.layout.map, canonical.map))
        throw std::invalid_argument("map pixels and published layout disagree");
    ValidateHud(snapshot.layout.hud, canonical.hud);
    if (snapshot.pitch < snapshot.width || snapshot.indices.size() < uint64_t{snapshot.pitch} * snapshot.height)
        throw std::invalid_argument("indexed map storage is truncated");
}
void PaintMapRow(std::vector<uint8_t>& output, const MapFrameSnapshot& frame, uint32_t row) {
    const auto width = static_cast<uint32_t>(frame.layout.output.width);
    const auto top = static_cast<uint32_t>(frame.layout.map.y);
    uint8_t* destination = output.data() + (uint64_t{row + top} * width) * 4;
    const uint8_t* source = frame.indices.data() + uint64_t{row} * frame.pitch;
    for (uint32_t column = 0; column < frame.width; ++column)
        std::memcpy(destination + column * 4, frame.palette[source[column]].data(), 4);
}
void PaintMap(std::vector<uint8_t>& output, const MapFrameSnapshot& snapshot) {
    for (uint32_t row = 0; row < snapshot.height; ++row)
        PaintMapRow(output, snapshot, row);
}
struct HudPainter {
    RgbaFrameView legacy;
    uint32_t output_width;
    aoe2::input::PresentationTransform transform;
};
void PaintHudRow(std::vector<uint8_t>& output, const HudPainter& painter, const HudRegion& region, uint32_t row) {
    const auto left = static_cast<uint32_t>(region.destination.x);
    const auto top = static_cast<uint32_t>(region.destination.y);
    const float logical_y = region.guest_source.y + row * region.guest_source.height / region.destination.height;
    for (uint32_t column = 0; column < region.destination.width; ++column) {
        const float logical_x = region.guest_source.x + column * region.guest_source.width / region.destination.width;
        const auto point = painter.transform.ToWindow(logical_x, logical_y);
        const auto source = uint64_t{static_cast<uint32_t>(point.y)} * painter.legacy.strideBytes + static_cast<uint32_t>(point.x) * 4;
        const auto target = (uint64_t{top + row} * painter.output_width + left + column) * 4;
        std::memcpy(output.data() + target, painter.legacy.pixels.data() + source, 4);
    }
}
void PaintHudRegion(std::vector<uint8_t>& output, const HudPainter& painter, const HudRegion& region) {
    for (uint32_t row = 0; row < region.destination.height; ++row)
        PaintHudRow(output, painter, region, row);
}
void PaintHud(std::vector<uint8_t>& output, RgbaFrameView legacy, const MapViewLayout& layout) {
    const aoe2::input::Extent extent{static_cast<float>(legacy.width), static_cast<float>(legacy.height)};
    const HudPainter painter{legacy, static_cast<uint32_t>(layout.output.width),
        aoe2::input::PresentationTransform::LogicalFillsFrame(extent, extent, {640, 448})};
    for (const auto& region : layout.hud.regions())
        PaintHudRegion(output, painter, region);
}
}
ComposedMapFrame MapViewCompositor::Compose(RgbaFrameView legacy, const MapFrameSnapshot& snapshot) {
    ValidateLegacy(legacy);
    ValidateMap(snapshot);
    const auto width = static_cast<uint32_t>(snapshot.layout.output.width);
    const auto height = static_cast<uint32_t>(snapshot.layout.output.height);
    auto pixels = std::make_shared<std::vector<uint8_t>>(uint64_t{width} * height * 4);
    PaintMap(*pixels, snapshot);
    PaintHud(*pixels, legacy, snapshot.layout);
    return {std::move(pixels), width, height, width * 4};
}
}
