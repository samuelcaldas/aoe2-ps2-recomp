#include <aoe2/assets/jasc_palette.hpp>
#include <aoe2/assets/slp_decoder.hpp>

#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

void Check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

aoe2::assets::JascPalette MakeTestPalette() {
    std::string text = "JASC-PAL\r\n0100\r\n256\r\n";
    for (int i = 0; i < 256; ++i) {
        text += std::to_string(i) + " " + std::to_string(i) + " " + std::to_string(i) + "\r\n";
    }
    return aoe2::assets::JascPalette::FromString(text);
}

void TestRealSlpSmall() {
    const std::string path = "game/hd-edition/source/resources/_common/drs/interface/53210.slp";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cout << "53210.slp not found, skipping real test\n";
        return;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto header = aoe2::assets::SlpDecoder::ReadHeader(data);
    Check(header.frame_count == 1, "frame count is 1");

    const auto palette = MakeTestPalette();
    const auto frame = aoe2::assets::SlpDecoder::DecodeFrame(data, 0, palette, 0);
    Check(frame.width == 10, "frame width 10");
    Check(frame.height == 10, "frame height 10");
    Check(frame.rgba_pixels.size() == 100, "pixel buffer size 100");

    // In 53210.slp, row 0 has left=3, right=3, so pixels 0..2 are transparent (alpha 0)
    Check((frame.rgba_pixels[0] >> 24) == 0, "row 0 pixel 0 transparent");
    // Pixel 3 is 255 (opaque white in our test palette)
    Check((frame.rgba_pixels[3] >> 24) == 255, "row 0 pixel 3 opaque");
    std::cout << "Verified 53210.slp decoded successfully\n";
}

void TestRealSlp50100IfPresent() {
    const std::string path = "game/hd-edition/source/resources/_common/drs/interface/50100.slp";
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return;
    }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    const auto palette = MakeTestPalette();
    const auto frame = aoe2::assets::SlpDecoder::DecodeFrame(data, 0, palette, 0);
    Check(frame.width == 800, "50100 width 800");
    Check(frame.height == 600, "50100 height 600");
    Check(frame.rgba_pixels.size() == 800 * 600, "50100 buffer size");
    std::cout << "Verified 50100.slp (800x600) decoded successfully\n";
}

} // namespace

int main() {
    TestRealSlpSmall();
    TestRealSlp50100IfPresent();
    std::cout << "slp_decoder_tests passed\n";
    return 0;
}
