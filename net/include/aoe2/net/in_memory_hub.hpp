#pragma once

#include <aoe2/net/network_transport.hpp>

#include <deque>
#include <map>
#include <memory>

namespace aoe2::net {

// In-process network for tests: hosts share one hub and exchange datagrams by
// (IPv4, port). Broadcast reaches every socket bound to the target port.
class InMemoryHub {
public:
    std::unique_ptr<ITransportFactory> CreateHost(uint32_t ipv4);

    bool Register(const Endpoint& local, std::deque<Datagram>* inbox);
    void Unregister(const Endpoint& local);
    uint16_t NextEphemeralPort();
    void Deliver(const Endpoint& source, const Endpoint& target, const uint8_t* data, std::size_t size);

private:
    std::map<std::pair<uint32_t, uint16_t>, std::deque<Datagram>*> inboxes_;
    uint16_t next_ephemeral_{49152};
};

} // namespace aoe2::net
