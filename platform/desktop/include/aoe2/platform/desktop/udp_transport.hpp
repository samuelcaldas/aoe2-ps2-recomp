#pragma once

#include <aoe2/net/network_transport.hpp>

namespace aoe2::platform {

// AOE2_NET_IPV4 (dotted quad) overrides detection; otherwise the first non-loopback
// IPv4 interface address is used, falling back to 127.0.0.1. Throws on a malformed override.
uint32_t DetectLocalIpv4();
// AOE2_NET_INSTANCE (0-65535, default 0) lets several processes share one host.
uint16_t InstanceFromEnvironment();

// Non-blocking IPv4 UDP sockets. With a loopback local address, broadcast is
// delivered to 127.0.0.1 so two processes on one host can discover each other.
class UdpTransportFactory : public aoe2::net::ITransportFactory {
public:
    explicit UdpTransportFactory(uint32_t local_ipv4) : local_ipv4_(local_ipv4) {}
    std::unique_ptr<aoe2::net::INetworkTransport> Create() override;

private:
    uint32_t local_ipv4_;
};

} // namespace aoe2::platform
