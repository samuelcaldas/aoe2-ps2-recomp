#include <aoe2/net/ilink_socket_model.hpp>

#include <algorithm>
#include <cstring>

namespace aoe2::net {
namespace {
constexpr uint32_t kFrameMagic = 0x32454F41u; // "AOE2"
constexpr std::size_t kHeaderBytes = 12;
constexpr int kOpenDomain = 60;
constexpr int kOpenType = 2;
constexpr uint32_t kInstanceMask = 0xFFFFu;

uint32_t Swap32(uint32_t value) { return __builtin_bswap32(value); }

void Put32(std::vector<uint8_t>& out, uint32_t value) {
    for (int shift = 0; shift < 4; ++shift) out.push_back(static_cast<uint8_t>(value >> (8 * shift)));
}

uint32_t Get32(const uint8_t* source) {
    uint32_t value = 0;
    for (int shift = 0; shift < 4; ++shift) value |= static_cast<uint32_t>(source[shift]) << (8 * shift);
    return value;
}

bool IsValidAddress(const IlinkAddress& address) {
    return address.family == kIlinkFamily;
}
}

IlinkSocketModel::IlinkSocketModel(ITransportFactory& factory, IlinkNode local_node)
    : factory_(factory), local_node_(local_node) {}

IlinkNode IlinkSocketModel::LocalEuiWords() const {
    return IlinkNode{Swap32(local_node_.high), Swap32(local_node_.low)};
}

IlinkSocketModel::Socket* IlinkSocketModel::Find(int socket) {
    if (socket < 0 || socket >= static_cast<int>(sockets_.size())) return nullptr;
    return &sockets_[socket];
}

int IlinkSocketModel::Open(int domain, int type, int) {
    if (domain != kOpenDomain || type != kOpenType) return kErrorInvalidArgument;
    if (static_cast<int>(sockets_.size()) >= kMaxSockets) return kErrorFailure;
    sockets_.push_back(Socket{factory_.Create(), std::nullopt, false});
    return static_cast<int>(sockets_.size()) - 1;
}

int IlinkSocketModel::Bind(int socket, const uint8_t* record, int length) {
    if (length != kIlinkAddressLength) return kErrorInvalidArgument;
    Socket* target = Find(socket);
    if (target == nullptr || target->bound) return kErrorFailure;
    const IlinkAddress address = ParseIlinkAddress(record, length);
    if (!IsValidAddress(address)) return kErrorInvalidArgument;
    const uint16_t instance = static_cast<uint16_t>(local_node_.high & kInstanceMask);
    const uint16_t port = address.port == 0 ? 0 : static_cast<uint16_t>(address.port + instance);
    target->bound = target->transport->Bind(port);
    return target->bound ? 0 : kErrorFailure;
}

int IlinkSocketModel::Connect(int socket, const uint8_t* record, int length) {
    if (length > kIlinkAddressLength || length < 0) return kErrorInvalidArgument;
    Socket* target = Find(socket);
    if (target == nullptr) return kErrorFailure;
    std::array<uint8_t, kIlinkAddressLength> padded{};
    std::memcpy(padded.data(), record, static_cast<std::size_t>(length));
    const IlinkAddress address = ParseIlinkAddress(padded.data(), padded.size());
    if (!IsValidAddress(address)) return kErrorInvalidArgument;
    target->peer = EndpointFor(address);
    return 0;
}

int IlinkSocketModel::Send(int socket, const uint8_t* data, int length) {
    if (length < 0 || length > static_cast<int>(kFrameBytes)) return kErrorInvalidArgument;
    Socket* target = Find(socket);
    if (target == nullptr || !target->peer) return kErrorFailure;
    std::vector<uint8_t> wire;
    Put32(wire, kFrameMagic);
    Put32(wire, local_node_.high);
    Put32(wire, local_node_.low);
    wire.insert(wire.end(), data, data + length);
    wire.resize(kHeaderBytes + kFrameBytes, 0);
    if (!target->transport->SendTo(*target->peer, wire.data(), wire.size())) return kErrorFailure;
    return static_cast<int>(kFrameBytes);
}

std::optional<IlinkSocketModel::Frame> IlinkSocketModel::NextFrame(Socket& socket) {
    while (auto datagram = socket.transport->TryReceive()) {
        const auto& wire = datagram->payload;
        if (wire.size() != kHeaderBytes + kFrameBytes || Get32(wire.data()) != kFrameMagic) continue;
        return Frame{IlinkNode{Get32(&wire[4]), Get32(&wire[8])},
                     std::vector<uint8_t>(wire.begin() + kHeaderBytes, wire.end())};
    }
    return std::nullopt;
}

int IlinkSocketModel::Deliver(const Frame& frame, uint8_t* out, int capacity) {
    const auto copied = std::min<std::size_t>(frame.payload.size(), static_cast<std::size_t>(capacity));
    std::memcpy(out, frame.payload.data(), copied);
    return static_cast<int>(frame.payload.size());
}

int IlinkSocketModel::Receive(int socket, uint8_t* out, int capacity) {
    if (capacity < 0 || capacity > static_cast<int>(kFrameBytes)) return kErrorInvalidArgument;
    Socket* target = Find(socket);
    if (target == nullptr) return kErrorFailure;
    const auto frame = NextFrame(*target);
    return frame ? Deliver(*frame, out, capacity) : 0;
}

int IlinkSocketModel::ReceiveFrom(int socket, uint8_t* out, int capacity, IlinkAddress& source) {
    if (capacity < 0 || capacity > static_cast<int>(kFrameBytes)) return kErrorInvalidArgument;
    Socket* target = Find(socket);
    if (target == nullptr) return kErrorFailure;
    const auto frame = NextFrame(*target);
    if (!frame) return 0;
    source = IlinkAddress{kIlinkAddressLength, kIlinkFamily, 0, frame->sender};
    return Deliver(*frame, out, capacity);
}

} // namespace aoe2::net
