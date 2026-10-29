#include <aoe2/input/map_input_router.hpp>
#include <algorithm>
#include <cmath>
#include <utility>

namespace aoe2::input {
namespace {
bool Contains(renderer::Rect rectangle, GuestPoint point) {
    return point.x >= rectangle.x && point.y >= rectangle.y &&
        point.x < rectangle.x + rectangle.width && point.y < rectangle.y + rectangle.height;
}
GuestPoint ClampTo(renderer::Rect rectangle, GuestPoint point) {
    return {std::clamp(point.x, rectangle.x, rectangle.x + rectangle.width - 1),
            std::clamp(point.y, rectangle.y, rectangle.y + rectangle.height - 1)};
}
std::optional<GuestPoint> OutputPoint(float x, float y, Extent window, Extent output, bool captured) {
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    const auto transform = PresentationTransform::LogicalFillsFrame(window, output, output);
    const auto destination = transform.FrameDestination();
    if (!captured && !Contains({destination.x, destination.y, destination.width, destination.height}, {x, y}))
        return std::nullopt;
    return transform.ToLogical(x, y);
}
RoutedMapPoint MapPoint(renderer::Rect rectangle, GuestPoint point) {
    const auto clamped = ClampTo(rectangle, point);
    return {MapInputRegion::Map, {clamped.x - rectangle.x, clamped.y - rectangle.y}};
}
std::optional<RoutedMapPoint> HudPoint(const std::vector<renderer::HudRegion>& regions, GuestPoint point) {
    const auto guest = HudInverseMapper(regions).ToGuest(point.x, point.y);
    if (!guest) return std::nullopt;
    return RoutedMapPoint{MapInputRegion::Hud, *guest};
}
std::optional<RoutedMapPoint> CapturedPoint(const MapPointerCapture& capture, renderer::Rect map, GuestPoint point) {
    if (capture.region == MapInputRegion::Map) return MapPoint(map, point);
    if (!capture.hud) return std::nullopt;
    return HudPoint({*capture.hud}, ClampTo(capture.hud->destination, point));
}
}
MapInputRouter::MapInputRouter(renderer::MapViewLayout layout) : layout_(std::move(layout)) {}
std::optional<RoutedMapPoint> MapInputRouter::Route(float x, float y, Extent window) const {
    const auto point = OutputPoint(x, y, window, layout_.output, capture_.has_value());
    if (!point) return std::nullopt;
    if (capture_) return CapturedPoint(*capture_, layout_.map, *point);
    if (Contains(layout_.map, *point)) return MapPoint(layout_.map, *point);
    return HudPoint(layout_.hud.regions(), *point);
}
void MapInputRouter::BeginDrag(float x, float y, Extent window) {
    capture_.reset();
    const auto point = OutputPoint(x, y, window, layout_.output, false);
    if (!point) return;
    if (Contains(layout_.map, *point)) {
        capture_ = MapPointerCapture{MapInputRegion::Map, std::nullopt};
        return;
    }
    for (const auto& region : layout_.hud.regions()) {
        if (Contains(region.destination, *point)) {
            capture_ = MapPointerCapture{MapInputRegion::Hud, region};
            return;
        }
    }
}
void MapInputRouter::EndDrag() { capture_.reset(); }
RoutedMapMouse MapInputRouter::RouteMouse(float x, float y, Extent window, uint32_t buttons) {
    if ((buttons & kMouseButtonLeft) && !(last_buttons_ & kMouseButtonLeft))
        BeginDrag(x, y, window);
    else if (!(buttons & kMouseButtonLeft) && (last_buttons_ & kMouseButtonLeft))
        EndDrag();
    last_buttons_ = buttons;
    suppressed_buttons_ &= buttons;
    auto point = Route(x, y, window);
    if (!point) suppressed_buttons_ |= buttons;
    return {point, point ? buttons & ~suppressed_buttons_ : 0u};
}
void MapInputRouter::ResetMouse(uint32_t buttons) {
    EndDrag();
    last_buttons_ = buttons;
    suppressed_buttons_ = buttons;
}
}
