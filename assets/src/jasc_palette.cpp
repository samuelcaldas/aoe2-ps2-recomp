#include <aoe2/assets/jasc_palette.hpp>

#include <charconv>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace aoe2::assets {

namespace {

std::string_view Trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) {
        s.remove_prefix(1);
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) {
        s.remove_suffix(1);
    }
    return s;
}

std::vector<std::string_view> SplitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find_first_of("\r\n", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = Trim(text.substr(start, end - start));
        if (!line.empty()) {
            lines.push_back(line);
        }
        if (end < text.size() && text[end] == '\r' && end + 1 < text.size() && text[end + 1] == '\n') {
            start = end + 2;
        } else {
            start = end + 1;
        }
    }
    return lines;
}

int ParseInt(std::string_view s) {
    int val = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
    if (ec != std::errc() || ptr != s.data() + s.size()) {
        throw std::runtime_error("JascPalette: invalid integer");
    }
    return val;
}

ColorRgba ParseColorLine(std::string_view line) {
    size_t p1 = line.find(' ');
    if (p1 == std::string_view::npos) throw std::runtime_error("JascPalette: invalid color format");
    size_t p2 = line.find(' ', p1 + 1);
    if (p2 == std::string_view::npos) throw std::runtime_error("JascPalette: invalid color format");

    int r = ParseInt(Trim(line.substr(0, p1)));
    int g = ParseInt(Trim(line.substr(p1 + 1, p2 - p1 - 1)));
    int b = ParseInt(Trim(line.substr(p2 + 1)));

    if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
        throw std::runtime_error("JascPalette: color channel out of range [0, 255]");
    }
    return ColorRgba{uint8_t(r), uint8_t(g), uint8_t(b), 255};
}

} // namespace

JascPalette JascPalette::FromBytes(std::span<const uint8_t> bytes) {
    return FromString(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

JascPalette JascPalette::FromString(std::string_view text) {
    auto lines = SplitLines(text);
    if (lines.size() < 3 + kColorCount) {
        throw std::runtime_error("JascPalette: too few lines in palette file");
    }
    if (lines[0] != "JASC-PAL") {
        throw std::runtime_error("JascPalette: invalid signature (expected JASC-PAL)");
    }
    if (lines[1] != "0100") {
        throw std::runtime_error("JascPalette: invalid version (expected 0100)");
    }
    int count = ParseInt(lines[2]);
    if (count != static_cast<int>(kColorCount)) {
        throw std::runtime_error("JascPalette: declared color count must be 256");
    }
    if (lines.size() != 3 + kColorCount) {
        throw std::runtime_error("JascPalette: line count does not match 256 colors");
    }

    JascPalette pal;
    for (size_t i = 0; i < kColorCount; ++i) {
        pal.colors_[i] = ParseColorLine(lines[3 + i]);
    }
    return pal;
}

} // namespace aoe2::assets
