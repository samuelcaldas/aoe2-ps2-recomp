#pragma once

#include <aoe2/net/network_transport.hpp>

#include <chrono>
#include <functional>
#include <string>

namespace aoe2::net {

inline constexpr uint8_t kLanDiscoveryVersion = 1;
inline constexpr size_t kMaxLanSessionNameBytes = 64;
inline constexpr size_t kMaxLanDiscoveryPacketBytes = 24 + kMaxLanSessionNameBytes;
inline constexpr size_t kMaxDiscoveredSessions = 256;

enum class DiscoveryError {
    None,
    MalformedPacket,
    UnsupportedVersion,
    IncompatibleSession,
    InvalidName,
    InvalidAdvertisement,
    PacketTooLarge,
    InvalidSource,
    DirectoryFull,
};

struct LanSessionAdvertisement {
    uint64_t session_id{0};
    std::string session_name;
    uint8_t players{0};
    uint8_t max_players{0};
    uint64_t compatibility_id{0};
};

struct DiscoveryEncodeResult {
    DiscoveryError error{DiscoveryError::None};
    std::vector<uint8_t> data;
    explicit operator bool() const { return error == DiscoveryError::None; }
};

struct DiscoveryDecodeResult {
    DiscoveryError error{DiscoveryError::None};
    std::optional<LanSessionAdvertisement> advertisement;
    explicit operator bool() const { return error == DiscoveryError::None; }
};

class LanDiscoveryCodec {
public:
    static DiscoveryEncodeResult Encode(const LanSessionAdvertisement& advertisement);
    static DiscoveryDecodeResult Decode(const std::vector<uint8_t>& packet, uint64_t expected_compatibility_id);
};

struct DiscoveredLanSession {
    LanSessionAdvertisement advertisement;
    Endpoint host;
};

struct DiscoveryRejection {
    Endpoint source;
    DiscoveryError error;
};

struct DiscoveryPollResult {
    size_t packets_processed{0};
    std::vector<DiscoveryRejection> rejected_packets;
};

class LanSessionDirectory {
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    using TimePoint = std::chrono::steady_clock::time_point;

    LanSessionDirectory(uint64_t compatibility_id, std::chrono::steady_clock::duration expiration, Clock clock);
    DiscoveryError Observe(const Datagram& packet);
    DiscoveryPollResult Poll(INetworkTransport& transport, size_t packet_budget);
    std::vector<DiscoveredLanSession> Sessions();

private:
    struct Entry {
        DiscoveredLanSession session;
        TimePoint last_seen;
    };

    void Expire(TimePoint now);
    uint64_t compatibility_id_;
    std::chrono::steady_clock::duration expiration_;
    Clock clock_;
    std::vector<Entry> entries_;
};

}
