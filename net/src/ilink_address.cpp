#include <aoe2/net/ilink_address.hpp>

#include <cstring>

namespace aoe2::net {
namespace {
constexpr uint32_t kNodeMarker = 0xAE20u << 16;
constexpr uint32_t kInstanceMask = 0xFFFFu;

void Store32(uint8_t* destination, uint32_t value) {
    for (int shift = 0; shift < 4; ++shift) destination[shift] = static_cast<uint8_t>(value >> (8 * shift));
}

uint32_t Load32(const uint8_t* source) {
    uint32_t value = 0;
    for (int shift = 0; shift < 4; ++shift) value |= static_cast<uint32_t>(source[shift]) << (8 * shift);
    return value;
}
}

IlinkAddressRecord SerializeIlinkAddress(const IlinkAddress& address) {
    IlinkAddressRecord record{};
    record[0] = address.length;
    record[1] = address.family;
    record[2] = static_cast<uint8_t>(address.port);
    record[3] = static_cast<uint8_t>(address.port >> 8);
    Store32(&record[4], address.node.high);
    Store32(&record[8], address.node.low);
    return record;
}

IlinkAddress ParseIlinkAddress(const uint8_t* record, std::size_t size) {
    if (record == nullptr || size < kIlinkAddressLength)
        throw std::invalid_argument("i.LINK address record needs 16 bytes");
    IlinkAddress address;
    address.length = record[0];
    address.family = record[1];
    address.port = static_cast<uint16_t>(record[2] | (record[3] << 8));
    address.node = IlinkNode{Load32(&record[4]), Load32(&record[8])};
    return address;
}

IlinkNode NodeFor(uint32_t ipv4, uint16_t instance) {
    return IlinkNode{kNodeMarker | instance, ipv4};
}

Endpoint EndpointFor(const IlinkAddress& address) {
    if (address.node == kBroadcastNode) return Endpoint{kBroadcastIpv4, address.port};
    const auto instance = static_cast<uint16_t>(address.node.high & kInstanceMask);
    return Endpoint{address.node.low, static_cast<uint16_t>(address.port + instance)};
}

} // namespace aoe2::net
