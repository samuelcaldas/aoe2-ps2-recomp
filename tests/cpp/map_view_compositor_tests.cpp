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
    check(pixels[(40 * 1280 + 1279) * 4 + 3] == 128);
    check(pixels[((40 + map.height - 1) * 1280) * 4 + 1] == 255);
    check(pixels[((40 + map.height - 1) * 1280 + 1279) * 4] == 255);
    check(pixels[(574 * 1280 + 320) * 4 + 1] == static_cast<uint8_t>(345));
    check(pixels[(574 * 1280 + 320) * 4 + 2] == 77);
    map.palette[0] = {17, 18, 19, 20};
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
}
int main() { native_pixels(); resize_and_validation(); return failures == 0 ? 0 : 1; }
