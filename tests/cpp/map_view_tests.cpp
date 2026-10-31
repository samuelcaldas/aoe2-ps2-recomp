#include <aoe2/game_hooks/map_view_config.hpp>
#include <sstream>
#include <aoe2/game_hooks/map_view_state.hpp>
#include <aoe2/renderer/map_view_layout.hpp>

#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
int failures = 0;
void check(bool condition) {
    if (condition) return;
    ++failures;
    std::cerr << "map view contract failed\n";
}
template<class Operation> void rejects(Operation operation) {
    try { operation(); }
    catch (const std::invalid_argument&) { return; }
    check(false);
}
void configuration() {
    using namespace aoe2::game_hooks;
    check(ReadMapViewConfig(nullptr, nullptr) == MapViewMode::Legacy);
    check(ReadMapViewConfig("legacy", nullptr) == MapViewMode::Legacy);
    check(ReadMapViewConfig("relocation", nullptr) == MapViewMode::Relocation);
    check(ReadMapViewConfig("native", nullptr) == MapViewMode::Native);
    {
        std::istringstream config("# opt in only when requested\n  mode = native  \n");
        check(ReadMapViewConfigFile(config) == MapViewMode::Native);
    }
    {
        std::istringstream config(" # comment\n\nmode=relocation\n");
        check(ReadMapViewConfigFile(config) == MapViewMode::Relocation);
    }
    {
        std::istringstream config("# no override\n");
        check(!ReadMapViewConfigFile(config));
    }
    check(DefaultMapOutput(false, false) == std::pair<uint32_t, uint32_t>{640, 448});
    check(DefaultMapOutput(true, false) == std::pair<uint32_t, uint32_t>{1280, 720});
    check(DefaultMapOutput(true, true) == std::pair<uint32_t, uint32_t>{1920, 1080});
    rejects([] { ReadMapViewConfig("", nullptr); });
    rejects([] { ReadMapViewConfig("NATIVE", nullptr); });
    rejects([] { ReadMapViewConfig("native ", nullptr); });
    rejects([] { ReadMapViewConfig("native", "320x130"); });
    rejects([] { ReadMapViewConfig("legacy", "320x130"); });
    rejects([] { ReadMapViewConfig(nullptr, "320x130"); });
    rejects([] {
        std::istringstream config("mode=native\nmode=relocation\n");
        ReadMapViewConfigFile(config);
    });
    rejects([] {
        std::istringstream config("resolution=1920x1080\n");
        ReadMapViewConfigFile(config);
    });
}
void surfaces() {
    using aoe2::game_hooks::IndexedMapSurfaceLayout;
    const auto legacy = IndexedMapSurfaceLayout::Calculate(640, 262, 8u << 20);
    check(legacy.pitch == 640 && legacy.visible_offset == 16 + 5 * 640);
    check(legacy.buffer_stride == 16 + 640 * 331 + 16);
    const auto padded = IndexedMapSurfaceLayout::Calculate(641, 262, 8u << 20);
    check(padded.pitch == 656 && padded.visible_offset == 16 + 5 * 656);
    const auto maximum = IndexedMapSurfaceLayout::Calculate(1920, 1080, 8u << 20);
    check(maximum.total_bytes <= (8u << 20));
    const auto native_1080p = IndexedMapSurfaceLayout::Calculate(1920, 708, 8u << 20);
    check(native_1080p.buffer_stride == 16 + 1920 * 777 + 16);
    check(native_1080p.total_bytes <= (8u << 20));
    rejects([] { IndexedMapSurfaceLayout::Calculate(1920, 1080, 1024); });
    rejects([] { IndexedMapSurfaceLayout::Calculate(0, 262, 8u << 20); });
    rejects([] { IndexedMapSurfaceLayout::Calculate(640, 0, 8u << 20); });
    rejects([] { IndexedMapSurfaceLayout::Calculate(UINT64_MAX, 262, 8u << 20); });
    rejects([] { IndexedMapSurfaceLayout::Calculate(640, UINT64_MAX, 8u << 20); });
}
void layouts() {
    using aoe2::renderer::MapViewLayout;
    const auto legacy_size = MapViewLayout::ForOutput(640, 448);
    check(legacy_size.map.width == 640 && legacy_size.map.height == 262);
    const auto wide = MapViewLayout::ForOutput(1920, 448);
    check(wide.map.width == 1920 && wide.map.height == 262);
    const auto handheld = MapViewLayout::ForOutput(1280, 720);
    check(handheld.map.x == 0 && handheld.map.y == 40);
    check(handheld.map.width == 1280 && handheld.map.height == 534);
    const auto docked = MapViewLayout::ForOutput(1920, 1080);
    check(docked.map.width == 1920 && docked.map.height == 708);
    check(docked.hud.regions().size() == 2);
    check(docked.hud.regions()[1].guest_source.y == 302);
    check(docked.hud.regions()[1].destination.y == 788);
    rejects([] { MapViewLayout::ForOutput(1921, 1080); });
    rejects([] { MapViewLayout::ForOutput(1280, 1081); });
    rejects([] { MapViewLayout::ForOutput(639, 448); });
}
}
int main() {
    configuration();
    surfaces();
    layouts();
    return failures == 0 ? 0 : 1;
}
