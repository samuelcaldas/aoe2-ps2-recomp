#pragma once
#include <aoe2/input/hud_inverse_mapper.hpp>
#include <aoe2/input/presentation_transform.hpp>
#include <aoe2/input/usb_mouse.hpp>
#include <aoe2/renderer/map_view_layout.hpp>
#include <optional>

namespace aoe2::input {
enum class MapInputRegion { Map, Hud };
struct RoutedMapPoint {
    MapInputRegion region;
    GuestPoint coordinates;
};
struct MapPointerCapture {
    MapInputRegion region;
    std::optional<renderer::HudRegion> hud;
};
struct RoutedMapMouse {
    std::optional<RoutedMapPoint> point;
    uint32_t buttons;
};
class MapInputRouter {
public:
    explicit MapInputRouter(renderer::MapViewLayout layout);
    /// Route window coordinates to local world pixels or legacy HUD coordinates; empty space returns no event.
    std::optional<RoutedMapPoint> Route(float x, float y, Extent window) const;
    /// Preserve the starting coordinate domain until release, including outside the mapped region.
    void BeginDrag(float x, float y, Extent window);
    void EndDrag();
    /// Filter presses begun outside the map and HUD until those buttons are released.
    RoutedMapMouse RouteMouse(float x, float y, Extent window, uint32_t buttons);
    /// Prevent buttons held across a presentation change from becoming new clicks.
    void ResetMouse(uint32_t buttons = 0);
private:
    renderer::MapViewLayout layout_;
    std::optional<MapPointerCapture> capture_;
    uint32_t last_buttons_ = 0;
    uint32_t suppressed_buttons_ = 0;
};
}
