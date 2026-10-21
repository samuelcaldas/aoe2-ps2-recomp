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

std::string escape_tty_text(std::string_view text) {
    constexpr char DIGITS[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char byte : text) {
        if (byte == '\n' || byte == '\t' || (byte >= 32 && byte != 127)) {
            result.push_back(static_cast<char>(byte));
            continue;
        }
        result += "\\x";
        result += DIGITS[byte >> 4];
        result += DIGITS[byte & 15];
    }
    return result;
}
}
