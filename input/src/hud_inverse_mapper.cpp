#include <aoe2/input/hud_inverse_mapper.hpp>

namespace aoe2::input {

namespace {

bool Contains(renderer::Rect rectangle, float x, float y) {
    return x >= rectangle.x && y >= rectangle.y &&
        x < rectangle.x + rectangle.width && y < rectangle.y + rectangle.height;
}

GuestPoint Map(renderer::Rect source, renderer::Rect destination, float x, float y) {
    const float relative_x = (x - destination.x) / destination.width;
    const float relative_y = (y - destination.y) / destination.height;
    return GuestPoint{
        source.x + relative_x * source.width,
        source.y + relative_y * source.height,
    };
}

} // namespace

HudInverseMapper::HudInverseMapper(const std::vector<renderer::HudRegion>& regions)
    : regions_(regions) {}

std::optional<GuestPoint> HudInverseMapper::ToGuest(float display_x, float display_y) const {
    for (const renderer::HudRegion& region : regions_) {
        if (Contains(region.destination, display_x, display_y)) {
            return Map(region.guest_source, region.destination, display_x, display_y);
        }
    }
    return std::nullopt;
}

} // namespace aoe2::input
