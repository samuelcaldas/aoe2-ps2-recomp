#pragma once

#include <aoe2/net/ilink_address.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace aoe2::net {

struct Datagram {
    Endpoint source;
    std::vector<uint8_t> payload;
};

// One non-blocking datagram socket. Implementations never block the guest thread.
class INetworkTransport {
public:
    virtual ~INetworkTransport() = default;
    // Port 0 asks for an ephemeral port. Returns false when the port is unavailable.
    virtual bool Bind(uint16_t port) = 0;
    virtual bool SendTo(const Endpoint& target, const uint8_t* data, std::size_t size) = 0;
    virtual std::optional<Datagram> TryReceive() = 0;
};

class ITransportFactory {
public:
    virtual ~ITransportFactory() = default;
    virtual std::unique_ptr<INetworkTransport> Create() = 0;
};

} // namespace aoe2::net
