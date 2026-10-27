#include <aoe2/assets/jasc_palette.hpp>
#include <cassert>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

void Check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

std::string MakeSamplePaletteText(int color_count = 256) {
    std::ostringstream ss;
    ss << "JASC-PAL\r\n0100\r\n" << color_count << "\r\n";
    for (int i = 0; i < color_count; ++i) {
        ss << (i % 256) << " " << ((i * 2) % 256) << " " << ((i * 3) % 256) << "\r\n";
    }
    return ss.str();
}

void TestValidPalette() {
    const std::string text = MakeSamplePaletteText(256);
    const auto pal = aoe2::assets::JascPalette::FromString(text);

    Check(pal.GetColor(0) == aoe2::assets::ColorRgba{0, 0, 0, 255}, "color 0");
    Check(pal.GetColor(1) == aoe2::assets::ColorRgba{1, 2, 3, 255}, "color 1");
    Check(pal.GetColor(255) == aoe2::assets::ColorRgba{255, (255 * 2) % 256, (255 * 3) % 256, 255}, "color 255");
    Check(pal.GetColorU32(0) == 0xFF000000, "color 0 to U32");
}

void TestInvalidHeaders() {
    bool caught = false;
    try {
        aoe2::assets::JascPalette::FromString("NOT-JASC\n0100\n256\n");
    } catch (const std::exception&) {
        caught = true;
    }
    Check(caught, "rejects bad signature");

    caught = false;
    try {
        aoe2::assets::JascPalette::FromString("JASC-PAL\n0200\n256\n");
    } catch (const std::exception&) {
        caught = true;
    }
    Check(caught, "rejects bad version");

    caught = false;
    try {
        aoe2::assets::JascPalette::FromString(MakeSamplePaletteText(128));
    } catch (const std::exception&) {
        caught = true;
    }
    Check(caught, "rejects count != 256");
}

void TestRealPaletteIfPresent() {
    const std::string path = "game/hd-edition/source/resources/_common/drs/interface/50507.bina";
    std::ifstream file(path);
    if (!file.is_open()) {
        return; // Optional if file not imported
    }
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto pal = aoe2::assets::JascPalette::FromString(content);
    Check(pal.GetColor(0) == aoe2::assets::ColorRgba{0, 0, 0, 255}, "real pal color 0");
    Check(pal.GetColor(1) == aoe2::assets::ColorRgba{128, 0, 0, 255}, "real pal color 1");
    std::cout << "Verified real palette 50507.bina successfully\n";
}

} // namespace

int main() {
    TestValidPalette();
    TestInvalidHeaders();
    TestRealPaletteIfPresent();
    std::cout << "jasc_palette_tests passed\n";
    return 0;
}
