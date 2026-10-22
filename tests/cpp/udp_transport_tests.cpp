#include <aoe2/platform/desktop/udp_transport.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
using namespace aoe2;

void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

std::optional<net::Datagram> Poll(net::INetworkTransport& transport) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (auto datagram = transport.TryReceive()) return datagram;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return std::nullopt;
}
}

int main() {
    constexpr uint32_t kLoopback = 0x7F000001u;
    platform::UdpTransportFactory factory(kLoopback);
    auto receiver = factory.Create();
    auto sender = factory.Create();
    Check(receiver->TryReceive() == std::nullopt, "non-blocking receive with no data");
    Check(receiver->Bind(47311), "bind receiver port");
    Check(!factory.Create()->Bind(47311), "second bind of one port fails");

    const std::vector<uint8_t> payload{1, 2, 3, 4};
    Check(sender->SendTo(net::Endpoint{kLoopback, 47311}, payload.data(), payload.size()), "unicast send");
    const auto unicast = Poll(*receiver);
    Check(unicast && unicast->payload == payload && unicast->source.ipv4 == kLoopback, "unicast received");

    Check(sender->SendTo(net::Endpoint{net::kBroadcastIpv4, 47311}, payload.data(), payload.size()), "broadcast send");
    const auto broadcast = Poll(*receiver);
    Check(broadcast && broadcast->payload == payload, "loopback host maps broadcast to 127.0.0.1");

    setenv("AOE2_NET_IPV4", "10.1.2.3", 1);
    Check(platform::DetectLocalIpv4() == 0x0A010203u, "IPv4 override");
    setenv("AOE2_NET_IPV4", "not-an-ip", 1);
    bool rejected = false;
    try { platform::DetectLocalIpv4(); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "malformed IPv4 override rejected");
    setenv("AOE2_NET_INSTANCE", "70000", 1);
    rejected = false;
    try { platform::InstanceFromEnvironment(); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "out-of-range instance rejected");
    std::cout << "udp_transport_tests passed\n";
}
