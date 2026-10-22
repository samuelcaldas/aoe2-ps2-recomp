#pragma once

#include <aoe2/net/network_transport.hpp>

namespace aoe2::net {

// Transport for platforms without a datagram backend: every bind fails, so the
// game's socket setup reports failure instead of pretending to be online.
class NullTransportFactory : public ITransportFactory {
public:
    std::unique_ptr<INetworkTransport> Create() override;
};

} // namespace aoe2::net
