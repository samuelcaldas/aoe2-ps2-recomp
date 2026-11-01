#include <aoe2/renderer/map_view_compositor.hpp>
#include <iostream>
#include <stdexcept>

using namespace aoe2::renderer;
namespace {
int failures = 0;
void check(bool value) { if (!value) { ++failures; std::cerr << "map compositor contract failed\n"; } }
template<class Operation> void rejects(Operation operation) {
    try { operation(); } catch (const std::invalid_argument&) { return; }
    check(false);
}
MapFrameSnapshot snapshot(uint32_t width, uint32_t height) {
    auto layout = MapViewLayout::ForOutput(width, height);
    const auto rows = static_cast<uint32_t>(layout.map.height);
    const uint32_t pitch = width + 16;
    MapFrameSnapshot result{width, rows, pitch, std::vector<uint8_t>(pitch * rows, 255), {}, layout};
    for (uint32_t row = 0; row < rows; ++row)
        for (uint32_t column = 0; column < width; ++column)
            result.indices[row * pitch + column] = static_cast<uint8_t>((column + row) % 2);
    result.palette[0] = {255, 0, 0, 255};
    result.palette[1] = {0, 255, 0, 128};
    result.palette[255] = {0, 0, 255, 255};
    return result;
}
std::vector<uint8_t> legacy_pixels() {
    std::vector<uint8_t> pixels(640 * 512 * 4);
    for (uint32_t offset = 0; offset < 640 * 512; ++offset) {
        pixels[offset * 4] = static_cast<uint8_t>(offset % 640);
        pixels[offset * 4 + 1] = static_cast<uint8_t>(offset / 640);
        pixels[offset * 4 + 2] = 77;
        pixels[offset * 4 + 3] = 255;
    }
    return pixels;
}
void native_pixels() {
    const auto source = legacy_pixels();
    const RgbaFrameView legacy{source, 640, 512, 640 * 4};
    auto map = snapshot(1280, 720);
    const auto frame = MapViewCompositor::Compose(legacy, map);
    check(frame.width == 1280 && frame.height == 720 && frame.strideBytes == 1280 * 4);
    const auto& pixels = *frame.pixels;
    check(pixels[(40 * 1280) * 4] == 255);
    check(pixels[(40 * 1280 + 1279) * 4 + 1] == 255);
    check(pixels[(40 * 1280 + 1279) * 4 + 3] == 255);
    check(pixels[((40 + map.height - 1) * 1280) * 4 + 1] == 255);
    check(pixels[((40 + map.height - 1) * 1280 + 1279) * 4] == 255);
    check(pixels[(574 * 1280 + 320) * 4 + 1] == static_cast<uint8_t>(345));
    check(pixels[(574 * 1280 + 320) * 4 + 2] == 77);
    map.palette[0] = {17, 18, 19, 255};
    const auto changed = MapViewCompositor::Compose(legacy, map);
    check((*changed.pixels)[(40 * 1280) * 4] == 17);
    check(pixels[(40 * 1280) * 4] == 255);
}
void resize_and_validation() {
    const auto source = legacy_pixels();
    const RgbaFrameView legacy{source, 640, 512, 640 * 4};
    auto map = snapshot(1920, 1080);
    const auto frame = MapViewCompositor::Compose(legacy, map);
    check(frame.width == 1920 && frame.height == 1080);
    check((*frame.pixels)[(80 * 1920 + 1919) * 4 + 1] == 255);
    rejects([&] { MapViewCompositor::Compose({source, 640, 512, 639 * 4}, map); });
    rejects([&] { MapViewCompositor::Compose({std::span(source).first(100), 640, 512, 2560}, map); });
    map.pitch = 1919;
    rejects([&] { MapViewCompositor::Compose(legacy, map); });
    map.pitch = 1936;
    map.indices.clear();
    rejects([&] { MapViewCompositor::Compose(legacy, map); });
    map = snapshot(1280, 720);
    map.layout.hud = MapViewLayout::ForOutput(640, 448).hud;
    rejects([&] { MapViewCompositor::Compose(legacy, map); });
}
void ui_composition_tests() {
    std::vector<uint8_t> ui_pixels(640 * 448 * 4, 0);
    for (uint32_t y = 0; y < 448; ++y) {
        for (uint32_t x = 0; x < 640; ++x) {
            size_t idx = (y * 640 + x) * 4;
            ui_pixels[idx + 0] = static_cast<uint8_t>(x % 256);
            ui_pixels[idx + 1] = static_cast<uint8_t>(y % 256);
            ui_pixels[idx + 2] = 200;
            ui_pixels[idx + 3] = 255;
        }
    }
    const RgbaFrameView ui_view{ui_pixels, 640, 448, 640 * 4};

    // Test 1: Handheld 720p (1280x720)
    auto composed_720 = MapViewCompositor::TryComposeUi(ui_view, 1280, 720);
    check(composed_720.has_value());
    check(composed_720->width == 1280 && composed_720->height == 720);
    check(composed_720->strideBytes == 1280 * 4);
    const auto& px_720 = *composed_720->pixels;
    // Left pillarbox border should be black (RGB 0,0,0, A 255)
    check(px_720[0] == 0 && px_720[1] == 0 && px_720[2] == 0 && px_720[3] == 255);
    check(px_720[(360 * 1280 + 50) * 4 + 0] == 0);
    check(px_720[(360 * 1280 + 50) * 4 + 3] == 255);
    // Center of screen should contain UI pixels
    size_t center_idx = (360 * 1280 + 640) * 4;
    check(px_720[center_idx + 2] == 200 && px_720[center_idx + 3] == 255);

    // Test 2: Docked 1080p (1920x1080)
    auto composed_1080 = MapViewCompositor::TryComposeUi(ui_view, 1920, 1080);
    check(composed_1080.has_value());
    check(composed_1080->width == 1920 && composed_1080->height == 1080);
    check(composed_1080->strideBytes == 1920 * 4);
    const auto& px_1080 = *composed_1080->pixels;
    // Pillarbox border
    check(px_1080[(540 * 1920 + 100) * 4 + 0] == 0);
    check(px_1080[(540 * 1920 + 100) * 4 + 3] == 255);
    // Center pixel
    size_t center_1080_idx = (540 * 1920 + 960) * 4;
    check(px_1080[center_1080_idx + 2] == 200 && px_1080[center_1080_idx + 3] == 255);

    // Test 3: 1:1 Direct copy (640x448)
    auto composed_direct = MapViewCompositor::TryComposeUi(ui_view, 640, 448);
    check(composed_direct.has_value());
    check(composed_direct->width == 640 && composed_direct->height == 448);
    check((*composed_direct->pixels)[(100 * 640 + 100) * 4 + 2] == 200);

    // Test 4: Validation failures
    check(!MapViewCompositor::TryComposeUi({ui_pixels, 0, 448, 2560}, 1280, 720).has_value());
    check(!MapViewCompositor::TryComposeUi({ui_pixels, 640, 0, 2560}, 1280, 720).has_value());
    check(!MapViewCompositor::TryComposeUi({std::span(ui_pixels).first(100), 640, 448, 2560}, 1280, 720).has_value());
    check(!MapViewCompositor::TryComposeUi(ui_view, 500, 720).has_value());
    check(!MapViewCompositor::TryComposeUi(ui_view, 1280, 300).has_value());
}
}
int main() { native_pixels(); resize_and_validation(); ui_composition_tests(); return failures == 0 ? 0 : 1; }
