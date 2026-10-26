#include <aoe2/net/lan_discovery.hpp>
#include <aoe2/net/in_memory_hub.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace aoe2::net;
using namespace std::chrono_literals;

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: "   \
                      << #condition << std::endl;                               \
            ++failures;                                                        \
        }                                                                       \
    } while (false)

LanSessionAdvertisement Advertisement(uint64_t id = 41) {
    return LanSessionAdvertisement{id, "River Valley", 1, 2, 0x12345678};
}

void TestAdvertisementRoundTrips() {
    const auto encoded = LanDiscoveryCodec::Encode(Advertisement());
    CHECK(encoded);
    const auto decoded = LanDiscoveryCodec::Decode(encoded.data, 0x12345678);
    CHECK(decoded);
    if (decoded) {
        CHECK(decoded.advertisement->session_id == 41);
        CHECK(decoded.advertisement->session_name == "River Valley");
        CHECK(decoded.advertisement->players == 1);
        CHECK(decoded.advertisement->max_players == 2);
        CHECK(decoded.advertisement->compatibility_id == 0x12345678);
    }
}

void TestRejectsUnsupportedVersion() {
    auto encoded = LanDiscoveryCodec::Encode(Advertisement());
    encoded.data[4] = static_cast<uint8_t>(kLanDiscoveryVersion + 1);
    CHECK(LanDiscoveryCodec::Decode(encoded.data, 0x12345678).error == DiscoveryError::UnsupportedVersion);
}

void TestRejectsIncompatibleSession() {
    const auto encoded = LanDiscoveryCodec::Encode(Advertisement());
    CHECK(LanDiscoveryCodec::Decode(encoded.data, 0x87654321).error == DiscoveryError::IncompatibleSession);
}

void TestRejectsInvalidUtf8SessionName() {
    auto advertisement = Advertisement();
    advertisement.session_name = std::string("invalid-") + char(0xc0) + char(0xaf);
    CHECK(LanDiscoveryCodec::Encode(advertisement).error == DiscoveryError::InvalidName);
}

void TestRejectsTruncatedAndOversizedPackets() {
    const auto encoded = LanDiscoveryCodec::Encode(Advertisement());
    auto truncated = encoded.data;
    truncated.pop_back();
    CHECK(LanDiscoveryCodec::Decode(truncated, 0x12345678).error == DiscoveryError::MalformedPacket);
    const std::vector<uint8_t> oversized(kMaxLanDiscoveryPacketBytes + 1, 0);
    CHECK(LanDiscoveryCodec::Decode(oversized, 0x12345678).error == DiscoveryError::PacketTooLarge);
}

void TestDuplicateAdvertisementRefreshesOneSession() {
    auto now = LanSessionDirectory::TimePoint{};
    LanSessionDirectory directory(0x12345678, 5s, [&now] { return now; });
    const auto bytes = LanDiscoveryCodec::Encode(Advertisement()).data;
    const Datagram first{Endpoint{0x7f000001, 3001}, bytes};
    CHECK(directory.Observe(first) == DiscoveryError::None);
    now += 2s;
    const Datagram duplicate{Endpoint{0x7f000001, 3001}, bytes};
    CHECK(directory.Observe(duplicate) == DiscoveryError::None);
    now += 4s;
    const auto sessions = directory.Sessions();
    CHECK(sessions.size() == 1);
    if (!sessions.empty()) CHECK(sessions.front().host.port == 3001);
}

void TestExpiredSessionsAreNotListed() {
    auto now = LanSessionDirectory::TimePoint{};
    LanSessionDirectory directory(0x12345678, 5s, [&now] { return now; });
    const auto bytes = LanDiscoveryCodec::Encode(Advertisement()).data;
    CHECK(directory.Observe(Datagram{Endpoint{0x7f000001, 3001}, bytes}) == DiscoveryError::None);
    now += 5s;
    CHECK(directory.Sessions().empty());
}

constexpr uint32_t kSenderHost = 0x0A000001;
constexpr uint32_t kReceiverHost = 0x0A000002;
constexpr uint16_t kReceiverPort = 3001;

struct Pair {
    InMemoryHub hub;
    std::unique_ptr<ITransportFactory> sender_host = hub.CreateHost(kSenderHost);
    std::unique_ptr<ITransportFactory> receiver_host = hub.CreateHost(kReceiverHost);
    std::unique_ptr<INetworkTransport> sender = sender_host->Create();
    std::unique_ptr<INetworkTransport> receiver = receiver_host->Create();
    Pair() {
        CHECK(sender->Bind(0));
        CHECK(receiver->Bind(kReceiverPort));
    }
    bool Send(const std::vector<uint8_t>& bytes) {
        return sender->SendTo(Endpoint{kReceiverHost, kReceiverPort}, bytes.data(), bytes.size());
    }
};

void TestPollReportsRejectedAdvertisements() {
    Pair pair;
    auto now = LanSessionDirectory::TimePoint{};
    LanSessionDirectory directory(0x12345678, 5s, [&now] { return now; });
    auto advertisement = Advertisement();
    advertisement.compatibility_id = 0x87654321;
    CHECK(pair.Send(LanDiscoveryCodec::Encode(advertisement).data));

    const auto result = directory.Poll(*pair.receiver, 4);
    CHECK(result.packets_processed == 1);
    CHECK(result.rejected_packets.size() == 1);
    if (!result.rejected_packets.empty()) {
        CHECK(result.rejected_packets.front().error == DiscoveryError::IncompatibleSession);
        CHECK(result.rejected_packets.front().source.ipv4 == kSenderHost);
    }
    CHECK(directory.Sessions().empty());
}

void TestPollReadsPacketsFromTransportWithinBudget() {
    Pair pair;
    auto now = LanSessionDirectory::TimePoint{};
    LanSessionDirectory directory(0x12345678, 5s, [&now] { return now; });
    for (const uint64_t id : {41u, 42u, 43u})
        CHECK(pair.Send(LanDiscoveryCodec::Encode(Advertisement(id)).data));
    CHECK(directory.Poll(*pair.receiver, 2).packets_processed == 2);
    CHECK(directory.Sessions().size() == 2);
    CHECK(directory.Poll(*pair.receiver, 2).packets_processed == 1);
    CHECK(directory.Sessions().size() == 3);
    CHECK(directory.Poll(*pair.receiver, 2).packets_processed == 0);
}

}

int main() {
    TestAdvertisementRoundTrips();
    TestRejectsUnsupportedVersion();
    TestRejectsIncompatibleSession();
    TestRejectsInvalidUtf8SessionName();
    TestRejectsTruncatedAndOversizedPackets();
    TestDuplicateAdvertisementRefreshesOneSession();
    TestExpiredSessionsAreNotListed();
    TestPollReportsRejectedAdvertisements();
    TestPollReadsPacketsFromTransportWithinBudget();
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
