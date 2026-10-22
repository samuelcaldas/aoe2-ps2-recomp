#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace aoe2::net {

constexpr uint8_t kIlinkAddressLength = 16;
constexpr uint8_t kIlinkFamily = 60;
constexpr uint32_t kBroadcastIpv4 = 0xFFFFFFFFu;

// Two 32-bit node words exactly as they appear in a guest address record.
struct IlinkNode {
    uint32_t high{0};
    uint32_t low{0};
    bool operator==(const IlinkNode&) const = default;
};
constexpr IlinkNode kBroadcastNode{0xFFFFFFFFu, 0xFFFFFFFFu};

// Host-side view of the 16-byte guest address record (little-endian fields).
struct IlinkAddress {
    uint8_t length{kIlinkAddressLength};
    uint8_t family{kIlinkFamily};
    uint16_t port{0};
    IlinkNode node{};
};
using IlinkAddressRecord = std::array<uint8_t, kIlinkAddressLength>;

struct Endpoint {
    uint32_t ipv4{0};
    uint16_t port{0};
    bool operator==(const Endpoint&) const = default;
};

IlinkAddressRecord SerializeIlinkAddress(const IlinkAddress& address);
// Throws std::invalid_argument when fewer than 16 bytes are supplied.
IlinkAddress ParseIlinkAddress(const uint8_t* record, std::size_t size);

// Deterministic node identity for this port: word 0 carries a marker and the
// instance number, word 1 the IPv4 address. Instances let several processes share a host.
IlinkNode NodeFor(uint32_t ipv4, uint16_t instance);
Endpoint EndpointFor(const IlinkAddress& address);

} // namespace aoe2::net
