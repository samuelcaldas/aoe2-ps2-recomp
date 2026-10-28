#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace aoe2::assets {

// One frame table entry of a PS2 "4.2k" shape. Offsets are relative to the shape's data.
struct Slp4kFrame {
    uint32_t command_offset = 0;
    uint32_t outline_offset = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    int16_t hotspot_x = 0;
    int16_t hotspot_y = 0;
};

// Reads the header and frame table of a "4.2k" shape. Pixel commands are not decoded here.
class Slp4kReader {
public:
    // Throws std::invalid_argument for a wrong tag or a frame table that does not fit in the data.
    static Slp4kReader Parse(const uint8_t* data, std::size_t size);

    std::size_t FrameCount() const;
    // Throws std::out_of_range if index is not below FrameCount().
    Slp4kFrame Frame(std::size_t index) const;

private:
    explicit Slp4kReader(std::vector<Slp4kFrame> frames);

    std::vector<Slp4kFrame> frames_;
};

} // namespace aoe2::assets
