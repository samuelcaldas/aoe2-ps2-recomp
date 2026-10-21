#include <aoe2/renderer/hud_layout.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>

namespace aoe2::renderer {

namespace {

void RequirePositive(float value, const char* name) {
    if (value > 0.0f) {
        return;
    }
    throw std::invalid_argument(std::string(name) + " must be positive");
}

void ValidateSource(Rect source, aoe2::input::Extent guest) {
    RequirePositive(source.width, "HUD source width");
    RequirePositive(source.height, "HUD source height");
    if (source.x < 0.0f || source.y < 0.0f || source.x + source.width > guest.width ||
        source.y + source.height > guest.height) {
        throw std::invalid_argument("HUD source rectangle exceeds guest extent");
    }
}

Rect Place(const HudRegionSpec& specification, aoe2::input::Extent guest, aoe2::input::Extent display) {
    if (guest.width == display.width && guest.height == display.height) {
        return specification.guest_source;
    }
    const float width = specification.destination_width;
    const float height = specification.destination_height;
    const float right = display.width - width - specification.margin_x;
    const float bottom = display.height - height - specification.margin_y;
    const float center_y = (display.height - height) / 2.0f;
    const float x = specification.anchor == HudAnchor::Right || specification.anchor == HudAnchor::TopRight ||
        specification.anchor == HudAnchor::BottomRight ? right : specification.margin_x;
    const float y = specification.anchor == HudAnchor::Bottom || specification.anchor == HudAnchor::BottomLeft ||
        specification.anchor == HudAnchor::BottomRight ? bottom :
        specification.anchor == HudAnchor::Left || specification.anchor == HudAnchor::Right ? center_y :
        specification.margin_y;
    return {x, y, width, height};
}

bool Overlaps(Rect first, Rect second) {
    return first.x < second.x + second.width && second.x < first.x + first.width &&
        first.y < second.y + second.height && second.y < first.y + first.height;
}

} // namespace

HudLayout HudLayout::ForExtent(
    aoe2::input::Extent display_extent,
    const std::vector<HudRegionSpec>& specifications)
{
    return ForExtent({640.0f, 480.0f}, display_extent, specifications);
}

HudLayout HudLayout::ForExtent(
    aoe2::input::Extent guest_extent,
    aoe2::input::Extent display_extent,
    const std::vector<HudRegionSpec>& specifications)
{
    RequirePositive(guest_extent.width, "guest width");
    RequirePositive(guest_extent.height, "guest height");
    RequirePositive(display_extent.width, "display width");
    RequirePositive(display_extent.height, "display height");
    std::vector<HudRegion> regions;
    for (const HudRegionSpec& specification : specifications) {
        ValidateSource(specification.guest_source, guest_extent);
        RequirePositive(specification.destination_width, "HUD destination width");
        RequirePositive(specification.destination_height, "HUD destination height");
        Rect destination = Place(specification, guest_extent, display_extent);
        if (destination.x < 0.0f || destination.y < 0.0f ||
            destination.x + destination.width > display_extent.width ||
            destination.y + destination.height > display_extent.height) {
            throw std::invalid_argument("HUD destination rectangle exceeds display extent");
        }
        if (std::any_of(regions.begin(), regions.end(), [&](const HudRegion& region) {
                return Overlaps(region.destination, destination);
            })) {
            throw std::invalid_argument("HUD destination rectangles overlap");
        }
        regions.push_back({specification.role, specification.guest_source, destination});
    }
    return HudLayout(std::move(regions));
}

const std::vector<HudRegion>& HudLayout::regions() const {
    return regions_;
}

HudLayout::HudLayout(std::vector<HudRegion> regions) : regions_(std::move(regions)) {}

} // namespace aoe2::renderer
