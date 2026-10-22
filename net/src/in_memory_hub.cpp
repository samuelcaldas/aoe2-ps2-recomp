#include <aoe2/net/in_memory_hub.hpp>

namespace aoe2::net {
namespace {
class InMemoryTransport : public INetworkTransport {
public:
    InMemoryTransport(InMemoryHub& hub, uint32_t ipv4) : hub_(hub), ipv4_(ipv4) {}
    ~InMemoryTransport() override { if (bound_) hub_.Unregister(Local()); }

    bool Bind(uint16_t port) override {
        if (bound_) return false;
        const uint16_t chosen = port != 0 ? port : hub_.NextEphemeralPort();
        bound_ = hub_.Register(Endpoint{ipv4_, chosen}, &inbox_);
        port_ = bound_ ? chosen : 0;
        return bound_;
    }

    bool SendTo(const Endpoint& target, const uint8_t* data, std::size_t size) override {
        if (!bound_ && !Bind(0)) return false;
        hub_.Deliver(Local(), target, data, size);
        return true;
    }

    std::optional<Datagram> TryReceive() override {
        if (inbox_.empty()) return std::nullopt;
        Datagram datagram = std::move(inbox_.front());
        inbox_.pop_front();
        return datagram;
    }

private:
    Endpoint Local() const { return Endpoint{ipv4_, port_}; }

    InMemoryHub& hub_;
    uint32_t ipv4_;
    uint16_t port_{0};
    bool bound_{false};
    std::deque<Datagram> inbox_;
};

class InMemoryHost : public ITransportFactory {
public:
    InMemoryHost(InMemoryHub& hub, uint32_t ipv4) : hub_(hub), ipv4_(ipv4) {}
    std::unique_ptr<INetworkTransport> Create() override {
        return std::make_unique<InMemoryTransport>(hub_, ipv4_);
    }

private:
    InMemoryHub& hub_;
    uint32_t ipv4_;
};
}

std::unique_ptr<ITransportFactory> InMemoryHub::CreateHost(uint32_t ipv4) {
    return std::make_unique<InMemoryHost>(*this, ipv4);
}

bool InMemoryHub::Register(const Endpoint& local, std::deque<Datagram>* inbox) {
    return inboxes_.emplace(std::make_pair(local.ipv4, local.port), inbox).second;
}

void InMemoryHub::Unregister(const Endpoint& local) {
    inboxes_.erase(std::make_pair(local.ipv4, local.port));
}

uint16_t InMemoryHub::NextEphemeralPort() {
    return next_ephemeral_++;
}

void InMemoryHub::Deliver(const Endpoint& source, const Endpoint& target, const uint8_t* data, std::size_t size) {
    const bool broadcast = target.ipv4 == kBroadcastIpv4;
    for (auto& [key, inbox] : inboxes_) {
        const bool matches = broadcast ? key.second == target.port : key == std::make_pair(target.ipv4, target.port);
        if (matches) inbox->push_back(Datagram{source, std::vector<uint8_t>(data, data + size)});
    }
}

} // namespace aoe2::net
