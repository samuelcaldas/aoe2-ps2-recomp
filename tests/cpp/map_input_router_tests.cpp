#include <aoe2/input/map_input_router.hpp>
#include <cmath>
#include <iostream>
#include <limits>
using namespace aoe2::input;
namespace {
int failures = 0;
void check(bool value) { if (!value) { ++failures; std::cerr << "map input contract failed\n"; } }
bool near(float a, float b) { return std::fabs(a - b) < 0.01f; }
void check_point(const std::optional<RoutedMapPoint>& point, MapInputRegion kind, float x, float y) {
    check(point && point->region == kind && near(point->coordinates.x, x) && near(point->coordinates.y, y));
}
void regions() {
    MapInputRouter router(aoe2::renderer::MapViewLayout::ForOutput(1280, 720));
    check_point(router.Route(0, 40, {1280, 720}), MapInputRegion::Map, 0, 0);
    check_point(router.Route(1279, 40, {1280, 720}), MapInputRegion::Map, 1279, 0);
    check_point(router.Route(0, 573, {1280, 720}), MapInputRegion::Map, 0, 533);
    check_point(router.Route(1279, 573, {1280, 720}), MapInputRegion::Map, 1279, 533);
    check_point(router.Route(640, 307, {1280, 720}), MapInputRegion::Map, 640, 267);
    check_point(router.Route(840, 652, {1280, 720}), MapInputRegion::Hud, 520, 380);
    check_point(router.Route(448, 20, {1280, 720}), MapInputRegion::Hud, 128, 20);
    check(!router.Route(0, 20, {1280, 720}));
    check(!router.Route(100, 600, {1280, 720}));
    check(!router.Route(0, 0, {1600, 1000}));
    check(!router.Route(std::numeric_limits<float>::quiet_NaN(), 100, {1280, 720}));
    check_point(router.Route(800, 500, {1600, 1000}), MapInputRegion::Map, 640, 320);
}
void drag_and_resize() {
    MapInputRouter router(aoe2::renderer::MapViewLayout::ForOutput(1280, 720));
    router.BeginDrag(640, 200, {1280, 720});
    check_point(router.Route(840, 652, {1280, 720}), MapInputRegion::Map, 840, 533);
    check_point(router.Route(-100, 200, {1280, 720}), MapInputRegion::Map, 0, 160);
    router.EndDrag();
    check_point(router.Route(840, 652, {1280, 720}), MapInputRegion::Hud, 520, 380);
    router.BeginDrag(840, 652, {1280, 720});
    check_point(router.Route(840, 300, {1280, 720}), MapInputRegion::Hud, 520, 302);
    router.EndDrag();
    MapInputRouter docked(aoe2::renderer::MapViewLayout::ForOutput(1920, 1080));
    check_point(docked.Route(1360, 944, {1920, 1080}), MapInputRegion::Hud, 520, 380);
    check_point(docked.Route(1919, 787, {1920, 1080}), MapInputRegion::Map, 1919, 707);
    check(!docked.Route(100, 800, {1920, 1080}));
}
void mouse_buttons() {
    MapInputRouter router(aoe2::renderer::MapViewLayout::ForOutput(1280, 720));
    const Extent window{1280, 720};
    auto result = router.RouteMouse(100, 600, window, kMouseButtonLeft);
    check(!result.point && result.buttons == 0);
    result = router.RouteMouse(640, 300, window, kMouseButtonLeft);
    check(result.point && result.buttons == 0); // A press in empty space stays suppressed.
    router.RouteMouse(640, 300, window, 0);
    result = router.RouteMouse(640, 300, window, kMouseButtonLeft);
    check(result.point && result.buttons == kMouseButtonLeft);
    result = router.RouteMouse(100, 600, window, kMouseButtonLeft);
    check_point(result.point, MapInputRegion::Map, 100, 533);
    check(result.buttons == kMouseButtonLeft); // The drag keeps its original domain.
    result = router.RouteMouse(100, 600, window, 0);
    check(!result.point && result.buttons == 0);
    router.ResetMouse(kMouseButtonRight);
    result = router.RouteMouse(640, 300, window, kMouseButtonRight);
    check(result.point && result.buttons == 0);
    router.RouteMouse(640, 300, window, 0);
    result = router.RouteMouse(640, 300, window, kMouseButtonRight);
    check(result.point && result.buttons == kMouseButtonRight);
    result = router.RouteMouse(100, 600, window, kMouseButtonRight);
    check(!result.point && result.buttons == 0);
    result = router.RouteMouse(640, 300, window, kMouseButtonRight);
    check(result.point && result.buttons == 0);
}
}
int main() { regions(); drag_and_resize(); mouse_buttons(); return failures == 0 ? 0 : 1; }
