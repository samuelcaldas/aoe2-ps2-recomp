#pragma once

#include <aoe2/net/network_transport.hpp>

#include <memory>
#include <optional>
#include <vector>

namespace aoe2::net {

constexpr int kMaxSockets = 16;
constexpr std::size_t kFrameBytes = 492;
// Wrapper-level error (argument size), returned by the game's own wrappers.
constexpr int kErrorInvalidArgument = -2007;
// Generic failure; the meaning of the guest's -2000 as a service error is unverified.
constexpr int kErrorFailure = -2000;

// Host model of the i.LINK socket service behind the game's socket wrappers.
// Calls never block: with nothing pending, receives return 0 (not a 492-byte frame).
class IlinkSocketModel {
public:
    IlinkSocketModel(ITransportFactory& factory, IlinkNode local_node);

    int Open(int domain, int type, int protocol);
    int Bind(int socket, const uint8_t* record, int length);
    int Connect(int socket, const uint8_t* record, int length);
    int Send(int socket, const uint8_t* data, int length);
    int Receive(int socket, uint8_t* out, int capacity);
    int ReceiveFrom(int socket, uint8_t* out, int capacity, IlinkAddress& source);

    // Node as carried in address records, and as the EUI words the guest reads
    // (the guest compares record nodes with the byte-swapped EUI words).
    IlinkNode LocalNode() const { return local_node_; }
    IlinkNode LocalEuiWords() const;

private:
    struct Socket {
        std::unique_ptr<INetworkTransport> transport;
        std::optional<Endpoint> peer;
        bool bound{false};
    };
    struct Frame {
        IlinkNode sender;
        std::vector<uint8_t> payload;
    };

    Socket* Find(int socket);
    std::optional<Frame> NextFrame(Socket& socket);
    int Deliver(const Frame& frame, uint8_t* out, int capacity);

    ITransportFactory& factory_;
    IlinkNode local_node_;
    std::vector<Socket> sockets_;
};

} // namespace aoe2::net
