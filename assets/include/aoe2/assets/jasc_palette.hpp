#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace aoe2::assets {

struct ColorRgba {
    uint8_t r{0};
    uint8_t g{0};
    uint8_t b{0};
    uint8_t a{255};

    constexpr uint32_t ToU32() const {
        return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
    }
    constexpr bool operator==(const ColorRgba& o) const = default;
};

class JascPalette {
public:
    static constexpr size_t kColorCount = 256;

    static JascPalette FromBytes(std::span<const uint8_t> bytes);
    static JascPalette FromString(std::string_view text);

    ColorRgba GetColor(uint8_t index) const { return colors_[index]; }
    uint32_t GetColorU32(uint8_t index) const { return colors_[index].ToU32(); }
    const std::array<ColorRgba, kColorCount>& Colors() const { return colors_; }

private:
    std::array<ColorRgba, kColorCount> colors_{};
};

} // namespace aoe2::assets
