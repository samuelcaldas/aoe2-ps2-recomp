#include <aoe2/assets/slp4k_reader.hpp>

#include <cstring>
#include <utility>
#include <stdexcept>

namespace aoe2::assets {

namespace {

constexpr std::size_t kTagSize = 4;
constexpr std::size_t kCountSize = 4;
constexpr std::size_t kHeaderSize = kTagSize + kCountSize;
constexpr std::size_t kEntrySize = 16;
constexpr char kTag[kTagSize] = {'4', '.', '2', 'k'};

uint16_t ReadU16(const uint8_t* at) {
    return static_cast<uint16_t>(at[0] | (at[1] << 8));
}

uint32_t ReadU32(const uint8_t* at) {
    return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) |
           (static_cast<uint32_t>(at[2]) << 16) | (static_cast<uint32_t>(at[3]) << 24);
}

Slp4kFrame ReadEntry(const uint8_t* at) {
    Slp4kFrame frame;
    frame.command_offset = ReadU32(at);
    frame.outline_offset = ReadU32(at + 4);
    frame.width = ReadU16(at + 8);
    frame.height = ReadU16(at + 10);
    frame.hotspot_x = static_cast<int16_t>(ReadU16(at + 12));
    frame.hotspot_y = static_cast<int16_t>(ReadU16(at + 14));
    return frame;
}

} // namespace

Slp4kReader::Slp4kReader(std::vector<Slp4kFrame> frames) : frames_(std::move(frames)) {}

Slp4kReader Slp4kReader::Parse(const uint8_t* data, std::size_t size) {
    if (data == nullptr || size < kHeaderSize || std::memcmp(data, kTag, kTagSize) != 0) {
        throw std::invalid_argument("not a 4.2k shape: bad tag or header");
    }
    const std::size_t count = ReadU32(data + kTagSize);
    if (count > (size - kHeaderSize) / kEntrySize) {
        throw std::invalid_argument("4.2k frame table exceeds the shape data");
    }
    std::vector<Slp4kFrame> frames;
    frames.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        frames.push_back(ReadEntry(data + kHeaderSize + index * kEntrySize));
    }
    return Slp4kReader(std::move(frames));
}

std::size_t Slp4kReader::FrameCount() const {
    return frames_.size();
}

Slp4kFrame Slp4kReader::Frame(std::size_t index) const {
    if (index >= frames_.size()) {
        throw std::out_of_range("4.2k frame index out of range");
    }
    return frames_[index];
}

} // namespace aoe2::assets
