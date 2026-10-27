#include <aoe2/assets/slp_decoder.hpp>

#include <cstring>
#include <stdexcept>

namespace aoe2::assets {

namespace {

uint16_t ReadU16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, 2);
    return v;
}

uint32_t ReadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

int32_t ReadS32(const uint8_t* p) {
    int32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

} // namespace

SlpHeaderInfo SlpDecoder::ReadHeader(std::span<const uint8_t> slp_bytes) {
    if (slp_bytes.size() < 32) {
        throw std::runtime_error("SlpDecoder: data smaller than SLP header (32 bytes)");
    }
    if (std::memcmp(slp_bytes.data(), "2.0N", 4) != 0) {
        throw std::runtime_error("SlpDecoder: invalid signature (expected 2.0N)");
    }
    SlpHeaderInfo info;
    std::memcpy(info.version, slp_bytes.data(), 4);
    info.version[4] = '\0';
    info.frame_count = ReadU32(slp_bytes.data() + 4);
    std::memcpy(info.comment, slp_bytes.data() + 8, 24);
    info.comment[24] = '\0';
    return info;
}

DecodedFrame SlpDecoder::DecodeFrame(
    std::span<const uint8_t> slp_bytes,
    uint32_t frame_index,
    const JascPalette& palette,
    uint8_t player_id
) {
    const auto header = ReadHeader(slp_bytes);
    if (frame_index >= header.frame_count) {
        throw std::runtime_error("SlpDecoder: frame_index out of range");
    }

    const size_t frame_header_off = 32 + frame_index * 32;
    if (frame_header_off + 32 > slp_bytes.size()) {
        throw std::runtime_error("SlpDecoder: truncated frame header");
    }

    const uint8_t* fptr = slp_bytes.data() + frame_header_off;
    const uint32_t cmd_table_off = ReadU32(fptr);
    const uint32_t outline_table_off = ReadU32(fptr + 4);
    const int32_t width = ReadS32(fptr + 16);
    const int32_t height = ReadS32(fptr + 20);
    const int32_t hotspot_x = ReadS32(fptr + 24);
    const int32_t hotspot_y = ReadS32(fptr + 28);

    if (width <= 0 || height <= 0 || width > 16384 || height > 16384) {
        throw std::runtime_error("SlpDecoder: invalid frame dimensions");
    }

    DecodedFrame frame;
    frame.width = width;
    frame.height = height;
    frame.hotspot_x = hotspot_x;
    frame.hotspot_y = hotspot_y;
    frame.rgba_pixels.assign(size_t(width) * size_t(height), 0);

    const size_t total_size = slp_bytes.size();
    if (outline_table_off + size_t(height) * 4 > total_size ||
        cmd_table_off + size_t(height) * 4 > total_size) {
        throw std::runtime_error("SlpDecoder: truncated outline or command table");
    }

    for (int32_t y = 0; y < height; ++y) {
        const uint16_t left = ReadU16(slp_bytes.data() + outline_table_off + y * 4);
        const uint16_t right = ReadU16(slp_bytes.data() + outline_table_off + y * 4 + 2);

        if (left == 0x8000 || right == 0x8000) {
            continue; // Whole row is transparent
        }

        const uint32_t row_cmd_off = ReadU32(slp_bytes.data() + cmd_table_off + y * 4);
        if (row_cmd_off >= total_size) {
            continue;
        }

        size_t cur = row_cmd_off;
        int32_t x = left;
        uint32_t* row_pixels = &frame.rgba_pixels[size_t(y) * size_t(width)];

        while (cur < total_size && x < width) {
            const uint8_t cmd = slp_bytes[cur++];
            if (cmd == 0x0F) {
                break; // End of row
            }

            const uint8_t low2 = cmd & 0x03;
            const uint8_t low4 = cmd & 0x0F;

            if (low2 == 0x00) {
                // Lesser Draw
                const size_t count = cmd >> 2;
                for (size_t i = 0; i < count && cur < total_size && x < width; ++i) {
                    row_pixels[x++] = palette.GetColorU32(slp_bytes[cur++]);
                }
            } else if (low2 == 0x01) {
                // Lesser Skip
                size_t count = cmd >> 2;
                if (count == 0 && cur < total_size) {
                    count = slp_bytes[cur++];
                }
                x += int32_t(count);
            } else if (low4 == 0x02) {
                // Greater Draw
                if (cur >= total_size) break;
                const size_t count = (size_t(cmd & 0xF0) << 4) + slp_bytes[cur++];
                for (size_t i = 0; i < count && cur < total_size && x < width; ++i) {
                    row_pixels[x++] = palette.GetColorU32(slp_bytes[cur++]);
                }
            } else if (low4 == 0x03) {
                // Greater Skip
                if (cur >= total_size) break;
                const size_t count = (size_t(cmd & 0xF0) << 4) + slp_bytes[cur++];
                x += int32_t(count);
            } else if (low4 == 0x06) {
                // Player Color Draw
                size_t count = cmd >> 4;
                if (count == 0 && cur < total_size) {
                    count = slp_bytes[cur++];
                }
                for (size_t i = 0; i < count && cur < total_size && x < width; ++i) {
                    const uint8_t offset = slp_bytes[cur++];
                    const uint8_t color_idx = uint8_t(player_id * 16 + offset);
                    row_pixels[x++] = palette.GetColorU32(color_idx);
                }
            } else if (low4 == 0x07) {
                // Fill
                size_t count = cmd >> 4;
                if (count == 0 && cur < total_size) {
                    count = slp_bytes[cur++];
                }
                if (cur >= total_size) break;
                const uint32_t color = palette.GetColorU32(slp_bytes[cur++]);
                for (size_t i = 0; i < count && x < width; ++i) {
                    row_pixels[x++] = color;
                }
            } else if (low4 == 0x0A) {
                // Fill Player Color
                size_t count = cmd >> 4;
                if (count == 0 && cur < total_size) {
                    count = slp_bytes[cur++];
                }
                if (cur >= total_size) break;
                const uint8_t offset = slp_bytes[cur++];
                const uint32_t color = palette.GetColorU32(uint8_t(player_id * 16 + offset));
                for (size_t i = 0; i < count && x < width; ++i) {
                    row_pixels[x++] = color;
                }
            } else if (low4 == 0x0B) {
                // Shadow Draw
                size_t count = cmd >> 4;
                if (count == 0 && cur < total_size) {
                    count = slp_bytes[cur++];
                }
                constexpr uint32_t shadow_color = 0x80000000; // 50% opacity black
                for (size_t i = 0; i < count && x < width; ++i) {
                    row_pixels[x++] = shadow_color;
                }
            } else if (low4 == 0x0E) {
                // Extended Commands
                if (cmd == 0x4E) {
                    if (x < width) {
                        row_pixels[x++] = palette.GetColorU32(uint8_t(player_id * 16));
                    }
                } else if (cmd == 0x5E && cur < total_size) {
                    const size_t count = slp_bytes[cur++];
                    const uint32_t color = palette.GetColorU32(uint8_t(player_id * 16));
                    for (size_t i = 0; i < count && x < width; ++i) {
                        row_pixels[x++] = color;
                    }
                } else if (cmd == 0x6E) {
                    if (x < width) {
                        row_pixels[x++] = palette.GetColorU32(243);
                    }
                } else if (cmd == 0x7E && cur < total_size) {
                    const size_t count = slp_bytes[cur++];
                    const uint32_t color = palette.GetColorU32(243);
                    for (size_t i = 0; i < count && x < width; ++i) {
                        row_pixels[x++] = color;
                    }
                } else {
                    break;
                }
            } else {
                // Unknown command, stop row decoding
                break;
            }
        }
    }

    return frame;
}

} // namespace aoe2::assets
