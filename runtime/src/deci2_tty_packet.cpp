#include "deci2_tty_packet.hpp"
#include <aoe2/runtime/deci2_host.hpp>

namespace aoe2::runtime {
namespace {
std::uint16_t read_word(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return bytes[offset] | (std::uint16_t(bytes[offset + 1]) << 8);
}

void validate_header(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 8) return;
    if (read_word(bytes, 2) != 0 || read_word(bytes, 4) != EE_TTY_PROTOCOL)
        throw Deci2Error("unsupported DECI2 TTY header");
    if (bytes[6] != 'E' || bytes[7] != 'H')
        throw Deci2Error("unsupported DECI2 TTY route");
}
}

std::optional<std::string> Deci2TtyPacket::append(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > 65535 - bytes_.size()) throw Deci2Error("DECI2 packet too large");
    bytes_.insert(bytes_.end(), bytes.begin(), bytes.end());
    if (bytes_.size() < 2) return std::nullopt;
    const auto expected = read_word(bytes_, 0);
    if (expected < 12 || bytes_.size() > expected) throw Deci2Error("invalid DECI2 packet length");
    validate_header(bytes_);
    if (bytes_.size() != expected) return std::nullopt;
    return std::string(bytes_.begin() + 12, bytes_.end());
}

void Deci2TtyPacket::clear() { bytes_.clear(); }

namespace {
std::size_t utf8_length(unsigned char lead) {
    if (lead >= 0xc2 && lead <= 0xdf) return 2;
    if (lead >= 0xe0 && lead <= 0xef) return 3;
    if (lead >= 0xf0 && lead <= 0xf4) return 4;
    return 0;
}

std::size_t printable_utf8(std::string_view text) {
    const auto length = utf8_length(static_cast<unsigned char>(text.front()));
    if (!length || length > text.size()) return 0;
    std::uint32_t point = static_cast<unsigned char>(text.front()) & (127u >> length);
    for (std::size_t index = 1; index < length; ++index) {
        const auto byte = static_cast<unsigned char>(text[index]);
        if ((byte & 0xc0) != 0x80) return 0;
        point = (point << 6) | (byte & 63);
    }
    constexpr std::uint32_t MINIMUM[] = {0, 0, 128, 2048, 65536};
    if (point < MINIMUM[length] || point < 160 || point > 0x10ffff) return 0;
    if (point >= 0xd800 && point <= 0xdfff) return 0;
    return length;
}

std::size_t append_display_text(std::string& result, std::string_view text) {
    const auto byte = static_cast<unsigned char>(text.front());
    auto length = printable_utf8(text);
    if (byte == '\n' || byte == '\t' || (byte >= 32 && byte < 127)) length = 1;
    if (length) { result.append(text.substr(0, length)); return length; }
    constexpr char DIGITS[] = "0123456789abcdef";
    result += "\\x";
    result += DIGITS[byte >> 4];
    result += DIGITS[byte & 15];
    return 1;
}
}

std::string escape_tty_text(std::string_view text) {
    std::string result;
    for (std::size_t index = 0; index < text.size();)
        index += append_display_text(result, text.substr(index));
    return result;
}
}
