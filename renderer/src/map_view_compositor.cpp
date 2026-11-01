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
    const auto left = static_cast<uint32_t>(frame.layout.map.x);
    uint8_t* destination = output.data() + (uint64_t{row + top} * width + left) * 4;
    const uint8_t* source = frame.indices.data() + uint64_t{row} * frame.pitch;
    for (uint32_t column = 0; column < frame.width; ++column) {
        const auto& foreground = frame.palette[source[column]];
        uint8_t* pixel = destination + column * 4;
        // Palette alpha comes from GS 0..128 values. The GS CPU presenter
        // converts that range to host RGBA with min(255, alpha * 2); preserve
        // the same semantics for the native expanded map surface.
        const uint32_t alpha = std::min<uint32_t>(255u, uint32_t{foreground[3]} * 2u);
        if (alpha == 255) {
            std::memcpy(pixel, foreground.data(), 4);
            pixel[3] = 255;
            continue;
        }
        if (alpha == 0) continue;
        const uint32_t inverse_alpha = 255 - alpha;
        for (size_t channel = 0; channel < 3; ++channel)
            pixel[channel] = static_cast<uint8_t>((uint32_t{foreground[channel]} * alpha +
                                                   uint32_t{pixel[channel]} * inverse_alpha + 127) / 255);
        pixel[3] = static_cast<uint8_t>(alpha + (uint32_t{pixel[3]} * inverse_alpha + 127) / 255);
    }
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
void PaintLegacyMapBackdrop(std::vector<uint8_t>& output, RgbaFrameView legacy,
                            const MapViewLayout& layout) {
    const aoe2::input::Extent extent{static_cast<float>(legacy.width), static_cast<float>(legacy.height)};
    const HudPainter painter{legacy, static_cast<uint32_t>(layout.output.width),
        aoe2::input::PresentationTransform::LogicalFillsFrame(extent, extent, {640, 448})};
    const auto output_width = static_cast<uint32_t>(layout.output.width);
    const auto map_width = static_cast<uint32_t>(layout.map.width);
    const auto map_height = static_cast<uint32_t>(layout.map.height);
    const auto map_left = static_cast<uint32_t>(layout.map.x);
    const auto map_top = static_cast<uint32_t>(layout.map.y);
    for (uint32_t row = 0; row < map_height; ++row) {
        const float logical_y = 40.0f + row * 262.0f / map_height;
        for (uint32_t column = 0; column < map_width; ++column) {
            const float logical_x = column * 640.0f / map_width;
            const auto point = painter.transform.ToWindow(logical_x, logical_y);
            const auto source = uint64_t{static_cast<uint32_t>(point.y)} * legacy.strideBytes +
                                static_cast<uint32_t>(point.x) * 4;
            const auto target = (uint64_t{map_top + row} * output_width + map_left + column) * 4;
            std::memcpy(output.data() + target, legacy.pixels.data() + source, 4);
        }
    }
}
bool ValidateLegacyNoThrow(RgbaFrameView frame) noexcept {
    if (frame.width == 0 || frame.height == 0 || frame.width > 1920 || frame.height > 1080)
        return false;
    if (frame.strideBytes < uint64_t{frame.width} * 4 || frame.pixels.size() < uint64_t{frame.strideBytes} * frame.height)
        return false;
    return true;
}
bool ValidateHudNoThrow(const HudLayout& actual, const HudLayout& expected) noexcept {
    if (actual.regions().size() != expected.regions().size())
        return false;
    for (std::size_t index = 0; index < actual.regions().size(); ++index) {
        const auto& region = actual.regions()[index];
        const auto& reference = expected.regions()[index];
        if (region.role != reference.role || !SameRectangle(region.guest_source, reference.guest_source) ||
            !SameRectangle(region.destination, reference.destination))
            return false;
    }
    return true;
}
bool ValidateMapNoThrow(const MapFrameSnapshot& snapshot) noexcept {
    if (!std::isfinite(snapshot.layout.output.width) || snapshot.layout.output.width < 640 || snapshot.layout.output.width > 1920 ||
        std::floor(snapshot.layout.output.width) != snapshot.layout.output.width ||
        !std::isfinite(snapshot.layout.output.height) || snapshot.layout.output.height < 448 || snapshot.layout.output.height > 1080 ||
        std::floor(snapshot.layout.output.height) != snapshot.layout.output.height)
        return false;
    const auto canonical = MapViewLayout::ForOutput(static_cast<uint32_t>(snapshot.layout.output.width),
                                                   static_cast<uint32_t>(snapshot.layout.output.height));
    if (snapshot.width != canonical.map.width || snapshot.height != canonical.map.height ||
        !SameRectangle(snapshot.layout.map, canonical.map))
        return false;
    if (!ValidateHudNoThrow(snapshot.layout.hud, canonical.hud))
        return false;
    if (snapshot.pitch < snapshot.width || snapshot.indices.size() < uint64_t{snapshot.pitch} * snapshot.height)
        return false;
    return true;
}
}
std::optional<ComposedMapFrame> MapViewCompositor::TryCompose(RgbaFrameView legacy, const MapFrameSnapshot& snapshot) noexcept {
    if (!ValidateLegacyNoThrow(legacy) || !ValidateMapNoThrow(snapshot))
        return std::nullopt;
    const auto width = static_cast<uint32_t>(snapshot.layout.output.width);
    const auto height = static_cast<uint32_t>(snapshot.layout.output.height);
    auto pixels = std::make_shared<std::vector<uint8_t>>(uint64_t{width} * height * 4);
    PaintLegacyMapBackdrop(*pixels, legacy, snapshot.layout);
    PaintMap(*pixels, snapshot);
    PaintHud(*pixels, legacy, snapshot.layout);
    return ComposedMapFrame{std::move(pixels), width, height, width * 4};
}
std::optional<ComposedMapFrame> MapViewCompositor::TryComposeNativeMap(const MapFrameSnapshot& snapshot) noexcept {
    if (!ValidateMapNoThrow(snapshot)) return std::nullopt;
    const auto width = static_cast<uint32_t>(snapshot.layout.output.width);
    const auto height = static_cast<uint32_t>(snapshot.layout.output.height);
    auto pixels = std::make_shared<std::vector<uint8_t>>(uint64_t{width} * height * 4);
    for (size_t i = 3; i < pixels->size(); i += 4) (*pixels)[i] = 255;
    PaintMap(*pixels, snapshot);
    return ComposedMapFrame{std::move(pixels), width, height, width * 4};
}
std::optional<ComposedMapFrame> MapViewCompositor::TryComposeUi(RgbaFrameView ui, uint32_t output_width, uint32_t output_height) noexcept {
    if (!ValidateLegacyNoThrow(ui)) return std::nullopt;
    if (output_width < 640 || output_width > 1920 || output_height < 448 || output_height > 1080)
        return std::nullopt;

    auto pixels = std::make_shared<std::vector<uint8_t>>(uint64_t{output_width} * output_height * 4, 0);
    for (size_t i = 3; i < pixels->size(); i += 4) (*pixels)[i] = 255;

    if (output_width == ui.width && output_height == ui.height) {
        for (uint32_t y = 0; y < ui.height; ++y) {
            std::memcpy(pixels->data() + uint64_t{y} * output_width * 4,
                        ui.pixels.data() + uint64_t{y} * ui.strideBytes,
                        ui.width * 4);
        }
        return ComposedMapFrame{std::move(pixels), output_width, output_height, output_width * 4};
    }

    try {
        const aoe2::input::Extent window{static_cast<float>(output_width), static_cast<float>(output_height)};
        const aoe2::input::Extent frame{640.0f, 512.0f};
        const aoe2::input::Extent logical{static_cast<float>(ui.width), static_cast<float>(ui.height)};
        const auto transform = aoe2::input::PresentationTransform(window, frame, logical, {0.0f, 0.0f, frame.width, frame.height});
        const auto dest = transform.FrameDestination();

        const uint32_t left = static_cast<uint32_t>(std::max(0.0f, dest.x));
        const uint32_t top = static_cast<uint32_t>(std::max(0.0f, dest.y));
        const uint32_t dest_w = static_cast<uint32_t>(std::min(static_cast<float>(output_width) - left, dest.width));
        const uint32_t dest_h = static_cast<uint32_t>(std::min(static_cast<float>(output_height) - top, dest.height));

        for (uint32_t dy = 0; dy < dest_h; ++dy) {
            uint8_t* dst_row = pixels->data() + (uint64_t{top + dy} * output_width + left) * 4;
            for (uint32_t dx = 0; dx < dest_w; ++dx) {
                const auto pt = transform.ToLogical(static_cast<float>(left + dx), static_cast<float>(top + dy));
                const uint32_t sx = std::clamp(static_cast<uint32_t>(pt.x), 0u, ui.width - 1u);
                const uint32_t sy = std::clamp(static_cast<uint32_t>(pt.y), 0u, ui.height - 1u);
                const uint8_t* src_px = ui.pixels.data() + uint64_t{sy} * ui.strideBytes + sx * 4;
                uint8_t* dst_px = dst_row + dx * 4;
                std::memcpy(dst_px, src_px, 4);
            }
        }
    } catch (...) {
        return std::nullopt;
    }

    return ComposedMapFrame{std::move(pixels), output_width, output_height, output_width * 4};
}
ComposedMapFrame MapViewCompositor::Compose(RgbaFrameView legacy, const MapFrameSnapshot& snapshot) {
    auto result = TryCompose(legacy, snapshot);
    if (!result) {
        ValidateLegacy(legacy);
        ValidateMap(snapshot);
        throw std::invalid_argument("map view composition failed");
    }
    return *result;
}
}
