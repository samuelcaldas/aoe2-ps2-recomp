#include <aoe2/net/null_transport.hpp>

namespace aoe2::net {
namespace {
class NullTransport : public INetworkTransport {
public:
    bool Bind(uint16_t) override { return false; }
    bool SendTo(const Endpoint&, const uint8_t*, std::size_t) override { return false; }
    std::optional<Datagram> TryReceive() override { return std::nullopt; }
};
}

std::unique_ptr<INetworkTransport> NullTransportFactory::Create() {
    return std::make_unique<NullTransport>();
}

} // namespace aoe2::net
