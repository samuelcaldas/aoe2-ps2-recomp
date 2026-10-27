#pragma once

#include <aoe2/assets/jasc_palette.hpp>
#include <cstdint>
#include <span>
#include <vector>

namespace aoe2::assets {

struct DecodedFrame {
    int32_t width{0};
    int32_t height{0};
    int32_t hotspot_x{0};
    int32_t hotspot_y{0};
    std::vector<uint32_t> rgba_pixels; // row-major, size = width * height
};

struct SlpHeaderInfo {
    uint32_t frame_count{0};
    char version[5]{};
    char comment[25]{};
};

class SlpDecoder {
public:
    static SlpHeaderInfo ReadHeader(std::span<const uint8_t> slp_bytes);

    static DecodedFrame DecodeFrame(
        std::span<const uint8_t> slp_bytes,
        uint32_t frame_index,
        const JascPalette& palette,
        uint8_t player_id = 0
    );
};

} // namespace aoe2::assets
