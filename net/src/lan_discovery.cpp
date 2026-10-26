#include <aoe2/net/lan_discovery.hpp>

#include <algorithm>
#include <stdexcept>

namespace aoe2::net {
namespace {

constexpr uint8_t kMagic[]{'A', '2', 'L', 'N'};
constexpr size_t kHeaderSize = 24;

bool IsContinuation(uint8_t byte) { return (byte & 0xc0) == 0x80; }

bool IsValidUtf8(const std::string& value) {
    size_t index = 0;
    while (index < value.size()) {
        const auto first = static_cast<uint8_t>(value[index]);
        if (first < 0x80) {
            if (first == 0) return false;
            ++index;
            continue;
        }
        size_t count = 0;
        uint8_t second_min = 0x80;
        uint8_t second_max = 0xbf;
        if (first >= 0xc2 && first <= 0xdf) count = 1;
        if (first >= 0xe0 && first <= 0xef) {
            count = 2;
            if (first == 0xe0) second_min = 0xa0;
            if (first == 0xed) second_max = 0x9f;
        }
        if (first >= 0xf0 && first <= 0xf4) {
            count = 3;
            if (first == 0xf0) second_min = 0x90;
            if (first == 0xf4) second_max = 0x8f;
        }
        if (count == 0 || index + count >= value.size()) return false;
        const auto second = static_cast<uint8_t>(value[index + 1]);
        if (second < second_min || second > second_max) return false;
        for (size_t offset = 2; offset <= count; ++offset)
            if (!IsContinuation(static_cast<uint8_t>(value[index + offset]))) return false;
        index += count + 1;
    }
    return true;
}

void AppendUint64(std::vector<uint8_t>& output, uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8)
        output.push_back(static_cast<uint8_t>(value >> shift));
}

uint64_t ReadUint64(const std::vector<uint8_t>& input, size_t offset) {
    uint64_t value = 0;
    for (size_t index = 0; index < 8; ++index) value = (value << 8) | input[offset + index];
    return value;
}

DiscoveryDecodeResult DecodeFailure(DiscoveryError error) { return {error, std::nullopt}; }

}

DiscoveryEncodeResult LanDiscoveryCodec::Encode(const LanSessionAdvertisement& advertisement) {
    if (advertisement.session_id == 0 || advertisement.compatibility_id == 0 ||
        advertisement.max_players == 0 || advertisement.players > advertisement.max_players)
        return {DiscoveryError::InvalidAdvertisement, {}};
    if (advertisement.session_name.empty() || advertisement.session_name.size() > kMaxLanSessionNameBytes ||
        !IsValidUtf8(advertisement.session_name))
        return {DiscoveryError::InvalidName, {}};

    std::vector<uint8_t> packet;
    packet.reserve(kHeaderSize + advertisement.session_name.size());
    packet.insert(packet.end(), std::begin(kMagic), std::end(kMagic));
    packet.push_back(kLanDiscoveryVersion);
    AppendUint64(packet, advertisement.compatibility_id);
    AppendUint64(packet, advertisement.session_id);
    packet.push_back(advertisement.players);
    packet.push_back(advertisement.max_players);
    packet.push_back(static_cast<uint8_t>(advertisement.session_name.size()));
    packet.insert(packet.end(), advertisement.session_name.begin(), advertisement.session_name.end());
    return {DiscoveryError::None, std::move(packet)};
}

DiscoveryDecodeResult LanDiscoveryCodec::Decode(const std::vector<uint8_t>& packet, uint64_t expected_compatibility_id) {
    if (packet.size() > kMaxLanDiscoveryPacketBytes) return DecodeFailure(DiscoveryError::PacketTooLarge);
    if (packet.size() < kHeaderSize) return DecodeFailure(DiscoveryError::MalformedPacket);
    if (!std::equal(std::begin(kMagic), std::end(kMagic), packet.begin()))
        return DecodeFailure(DiscoveryError::MalformedPacket);
    if (packet[4] != kLanDiscoveryVersion) return DecodeFailure(DiscoveryError::UnsupportedVersion);

    const auto compatibility_id = ReadUint64(packet, 5);
    const auto session_id = ReadUint64(packet, 13);
    const auto players = packet[21];
    const auto max_players = packet[22];
    const auto name_size = packet[23];
    if (packet.size() != kHeaderSize + name_size) return DecodeFailure(DiscoveryError::MalformedPacket);
    if (compatibility_id != expected_compatibility_id) return DecodeFailure(DiscoveryError::IncompatibleSession);
    if (session_id == 0 || compatibility_id == 0 || max_players == 0 || players > max_players)
        return DecodeFailure(DiscoveryError::InvalidAdvertisement);

    LanSessionAdvertisement advertisement;
    advertisement.session_id = session_id;
    advertisement.compatibility_id = compatibility_id;
    advertisement.players = players;
    advertisement.max_players = max_players;
    advertisement.session_name.assign(packet.begin() + kHeaderSize, packet.end());
    if (advertisement.session_name.empty() || !IsValidUtf8(advertisement.session_name))
        return DecodeFailure(DiscoveryError::InvalidName);
    return {DiscoveryError::None, std::move(advertisement)};
}

LanSessionDirectory::LanSessionDirectory(uint64_t compatibility_id,
                                         std::chrono::steady_clock::duration expiration,
                                         Clock clock)
    : compatibility_id_(compatibility_id), expiration_(expiration), clock_(std::move(clock)) {
    if (compatibility_id_ == 0 || expiration_ <= expiration_.zero() || !clock_)
        throw std::invalid_argument("invalid LAN discovery directory configuration");
}

DiscoveryError LanSessionDirectory::Observe(const Datagram& packet) {
    if (packet.source.ipv4 == 0 || packet.source.port == 0) return DiscoveryError::InvalidSource;
    const auto decoded = LanDiscoveryCodec::Decode(packet.payload, compatibility_id_);
    if (!decoded) return decoded.error;
    const auto now = clock_();
    Expire(now);
    const auto matches = [&decoded, &packet](const Entry& entry) {
        return entry.session.advertisement.session_id == decoded.advertisement->session_id &&
               entry.session.host == packet.source;
    };
    auto existing = std::find_if(entries_.begin(), entries_.end(), matches);
    if (existing == entries_.end()) {
        if (entries_.size() >= kMaxDiscoveredSessions) return DiscoveryError::DirectoryFull;
        entries_.push_back(Entry{DiscoveredLanSession{*decoded.advertisement, packet.source}, now});
        return DiscoveryError::None;
    }
    existing->session.advertisement = *decoded.advertisement;
    existing->last_seen = now;
    return DiscoveryError::None;
}

DiscoveryPollResult LanSessionDirectory::Poll(INetworkTransport& transport, size_t packet_budget) {
    DiscoveryPollResult result;
    while (result.packets_processed < packet_budget) {
        auto packet = transport.TryReceive();
        if (!packet) return result;
        const auto error = Observe(*packet);
        if (error != DiscoveryError::None)
            result.rejected_packets.push_back(DiscoveryRejection{packet->source, error});
        ++result.packets_processed;
    }
    return result;
}

std::vector<DiscoveredLanSession> LanSessionDirectory::Sessions() {
    Expire(clock_());
    std::vector<DiscoveredLanSession> sessions;
    sessions.reserve(entries_.size());
    for (const auto& entry : entries_) sessions.push_back(entry.session);
    std::sort(sessions.begin(), sessions.end(), [](const auto& left, const auto& right) {
        if (left.advertisement.session_id != right.advertisement.session_id)
            return left.advertisement.session_id < right.advertisement.session_id;
        if (left.host.ipv4 != right.host.ipv4) return left.host.ipv4 < right.host.ipv4;
        return left.host.port < right.host.port;
    });
    return sessions;
}

void LanSessionDirectory::Expire(TimePoint now) {
    std::erase_if(entries_, [this, now](const Entry& entry) {
        return now - entry.last_seen >= expiration_;
    });
}

}
