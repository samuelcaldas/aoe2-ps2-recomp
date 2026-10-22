#include <aoe2/platform/desktop/udp_transport.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <charconv>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace aoe2::platform {
namespace {
using aoe2::net::Datagram;
using aoe2::net::Endpoint;
constexpr uint32_t kLoopbackIpv4 = 0x7F000001u;
constexpr std::size_t kMaxDatagramBytes = 2048;

sockaddr_in AddressOf(uint32_t ipv4, uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(ipv4);
    address.sin_port = htons(port);
    return address;
}

class UdpTransport : public aoe2::net::INetworkTransport {
public:
    explicit UdpTransport(uint32_t local_ipv4) : local_ipv4_(local_ipv4) {
        descriptor_ = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (descriptor_ < 0) throw std::runtime_error("cannot create UDP socket");
        const int enabled = 1;
        ::setsockopt(descriptor_, SOL_SOCKET, SO_BROADCAST, &enabled, sizeof(enabled));
        ::fcntl(descriptor_, F_SETFL, ::fcntl(descriptor_, F_GETFL, 0) | O_NONBLOCK);
    }
    ~UdpTransport() override { ::close(descriptor_); }

    bool Bind(uint16_t port) override {
        const sockaddr_in address = AddressOf(0, port);
        return ::bind(descriptor_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0;
    }

    bool SendTo(const Endpoint& target, const uint8_t* data, std::size_t size) override {
        const sockaddr_in address = AddressOf(Resolve(target.ipv4), target.port);
        const auto sent = ::sendto(descriptor_, data, size, 0,
                                   reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        return sent == static_cast<ssize_t>(size);
    }

    std::optional<Datagram> TryReceive() override {
        std::vector<uint8_t> buffer(kMaxDatagramBytes);
        sockaddr_in from{};
        socklen_t from_size = sizeof(from);
        const auto received = ::recvfrom(descriptor_, buffer.data(), buffer.size(), 0,
                                         reinterpret_cast<sockaddr*>(&from), &from_size);
        if (received < 0) return std::nullopt;
        buffer.resize(static_cast<std::size_t>(received));
        return Datagram{Endpoint{ntohl(from.sin_addr.s_addr), ntohs(from.sin_port)}, std::move(buffer)};
    }

private:
    uint32_t Resolve(uint32_t ipv4) const {
        const bool loopback_host = local_ipv4_ == kLoopbackIpv4;
        return ipv4 == aoe2::net::kBroadcastIpv4 && loopback_host ? kLoopbackIpv4 : ipv4;
    }

    int descriptor_{-1};
    uint32_t local_ipv4_;
};

uint32_t ParseIpv4(const std::string& text) {
    in_addr parsed{};
    if (::inet_pton(AF_INET, text.c_str(), &parsed) != 1)
        throw std::invalid_argument("AOE2_NET_IPV4 is not a dotted IPv4 address: " + text);
    return ntohl(parsed.s_addr);
}

uint32_t FirstNonLoopbackIpv4() {
    ifaddrs* list = nullptr;
    if (::getifaddrs(&list) != 0) return kLoopbackIpv4;
    uint32_t found = kLoopbackIpv4;
    for (const ifaddrs* entry = list; entry != nullptr; entry = entry->ifa_next) {
        if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET) continue;
        const uint32_t candidate = ntohl(reinterpret_cast<const sockaddr_in*>(entry->ifa_addr)->sin_addr.s_addr);
        if ((candidate >> 24) == 127) continue;
        found = candidate;
        break;
    }
    ::freeifaddrs(list);
    return found;
}
}

uint32_t DetectLocalIpv4() {
    const char* override_value = std::getenv("AOE2_NET_IPV4");
    if (override_value != nullptr && *override_value != '\0') return ParseIpv4(override_value);
    return FirstNonLoopbackIpv4();
}

uint16_t InstanceFromEnvironment() {
    const char* text = std::getenv("AOE2_NET_INSTANCE");
    if (text == nullptr || *text == '\0') return 0;
    unsigned value = 0;
    const std::string_view view(text);
    const auto [end, error] = std::from_chars(view.data(), view.data() + view.size(), value);
    if (error != std::errc{} || end != view.data() + view.size() || value > 0xFFFF)
        throw std::invalid_argument(std::string("AOE2_NET_INSTANCE must be 0-65535: ") + text);
    return static_cast<uint16_t>(value);
}

std::unique_ptr<aoe2::net::INetworkTransport> UdpTransportFactory::Create() {
    return std::make_unique<UdpTransport>(local_ipv4_);
}

} // namespace aoe2::platform
