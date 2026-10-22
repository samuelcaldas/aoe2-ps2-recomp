#include <aoe2/net/ilink_address.hpp>
#include <aoe2/net/ilink_socket_model.hpp>
#include <aoe2/net/in_memory_hub.hpp>

#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
using namespace aoe2::net;

void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

constexpr uint32_t kHostA = 0x0A000001;
constexpr uint32_t kHostB = 0x0A000002;
constexpr int kOpen[3] = {60, 2, 0};

IlinkAddressRecord Record(uint16_t port, IlinkNode node) {
    return SerializeIlinkAddress(IlinkAddress{16, 60, port, node});
}

struct Peer {
    Peer(InMemoryHub& hub, uint32_t ipv4, uint16_t instance)
        : transports(hub.CreateHost(ipv4)), model(*transports, NodeFor(ipv4, instance)) {}
    std::unique_ptr<ITransportFactory> transports;
    IlinkSocketModel model;
    int Open() { return model.Open(kOpen[0], kOpen[1], kOpen[2]); }
};

std::vector<uint8_t> Frame(uint8_t fill) { return std::vector<uint8_t>(492, fill); }

void TestAddressRecordRoundTrip() {
    const IlinkAddress original{16, 60, 0x3412, IlinkNode{0x11223344, 0x55667788}};
    const auto record = SerializeIlinkAddress(original);
    Check(record[0] == 16 && record[1] == 60, "record header");
    const auto parsed = ParseIlinkAddress(record.data(), record.size());
    Check(parsed.port == 0x3412 && parsed.node == original.node, "record round trip");
    bool rejected = false;
    try { ParseIlinkAddress(record.data(), 8); } catch (const std::invalid_argument&) { rejected = true; }
    Check(rejected, "short record rejected");
}

void TestNodeMapping() {
    const auto node = NodeFor(kHostA, 3);
    Check(EndpointFor(IlinkAddress{16, 60, 100, node}) == (Endpoint{kHostA, 103}), "node maps to host + instance port");
    Check(EndpointFor(IlinkAddress{16, 60, 100, kBroadcastNode}).ipv4 == kBroadcastIpv4, "all-ones is broadcast");
}

void TestSocketLifecycle() {
    InMemoryHub hub;
    Peer peer(hub, kHostA, 0);
    for (int index = 0; index < kMaxSockets; ++index) Check(peer.Open() == index, "sequential socket ids");
    Check(peer.Open() == kErrorFailure, "socket table exhausted");
    Check(peer.model.Open(1, 2, 0) == kErrorInvalidArgument, "unsupported domain rejected");
    const auto record = Record(0x3412, IlinkNode{0, 0});
    Check(peer.model.Bind(99, record.data(), 16) == kErrorFailure, "unknown socket");
    Check(peer.model.Bind(0, record.data(), 8) == kErrorInvalidArgument, "bind length must be 16");
    Check(peer.model.Bind(0, record.data(), 16) == 0, "bind succeeds");
    Check(peer.model.Bind(1, record.data(), 16) == kErrorFailure, "duplicate bind of one port fails");
}

void TestSizeRules() {
    InMemoryHub hub;
    Peer peer(hub, kHostA, 0);
    const int socket = peer.Open();
    const std::vector<uint8_t> oversize(493, 0);
    std::vector<uint8_t> buffer(493);
    Check(peer.model.Send(socket, oversize.data(), 493) == kErrorInvalidArgument, "send 493 rejected");
    Check(peer.model.Receive(socket, buffer.data(), 493) == kErrorInvalidArgument, "recv 493 rejected");
    IlinkAddress source;
    Check(peer.model.ReceiveFrom(socket, buffer.data(), 493, source) == kErrorInvalidArgument, "recvfrom 493 rejected");
    Check(peer.model.Send(socket, oversize.data(), 492) == kErrorFailure, "send without peer fails");
}

void TestNoDataIsNotAPacket() {
    InMemoryHub hub;
    Peer peer(hub, kHostA, 0);
    const int socket = peer.Open();
    const auto record = Record(0x3412, IlinkNode{0, 0});
    peer.model.Bind(socket, record.data(), 16);
    std::vector<uint8_t> buffer(492);
    IlinkAddress source;
    Check(peer.model.Receive(socket, buffer.data(), 492) == 0, "recv without data returns 0");
    Check(peer.model.ReceiveFrom(socket, buffer.data(), 492, source) == 0, "recvfrom without data returns 0");
}

void TestLoopbackBetweenTwoModels() {
    InMemoryHub hub;
    Peer sender(hub, kHostA, 0);
    Peer receiver(hub, kHostB, 0);
    const int rx = receiver.Open();
    const auto rx_record = Record(0x3412, IlinkNode{0, 0});
    Check(receiver.model.Bind(rx, rx_record.data(), 16) == 0, "receiver bind");
    const int tx = sender.Open();
    const auto target = Record(0x3412, receiver.model.LocalNode());
    Check(sender.model.Connect(tx, target.data(), 16) == 0, "sender connect");
    const auto payload = Frame(0x5A);
    Check(sender.model.Send(tx, payload.data(), 492) == 492, "send returns 492");
    std::vector<uint8_t> buffer(492);
    IlinkAddress source;
    Check(receiver.model.ReceiveFrom(rx, buffer.data(), 492, source) == 492, "recvfrom returns 492");
    Check(buffer == payload, "payload preserved");
    Check(source.node == sender.model.LocalNode() && source.family == 60 && source.length == 16, "source is the sender node");
    Check(receiver.model.Receive(rx, buffer.data(), 492) == 0, "datagram consumed once");
}

void TestBroadcastReachesEveryBoundSocket() {
    InMemoryHub hub;
    Peer first(hub, kHostA, 0);
    Peer second(hub, kHostB, 0);
    const auto rx_record = Record(0x3412, IlinkNode{0, 0});
    const int first_rx = first.Open();
    const int second_rx = second.Open();
    first.model.Bind(first_rx, rx_record.data(), 16);
    second.model.Bind(second_rx, rx_record.data(), 16);
    const int broadcast = first.Open();
    const auto target = Record(0x3412, kBroadcastNode);
    Check(first.model.Connect(broadcast, target.data(), 16) == 0, "connect to broadcast node");
    const auto payload = Frame(1);
    Check(first.model.Send(broadcast, payload.data(), 492) == 492, "broadcast send");
    std::vector<uint8_t> buffer(492);
    IlinkAddress source;
    Check(second.model.ReceiveFrom(second_rx, buffer.data(), 492, source) == 492, "other host receives broadcast");
    Check(first.model.ReceiveFrom(first_rx, buffer.data(), 492, source) == 492, "sender also hears its own broadcast");
}

void TestShortSendIsPaddedToFrame() {
    InMemoryHub hub;
    Peer sender(hub, kHostA, 0);
    Peer receiver(hub, kHostB, 0);
    const int rx = receiver.Open();
    const auto rx_record = Record(0x3412, IlinkNode{0, 0});
    receiver.model.Bind(rx, rx_record.data(), 16);
    const int tx = sender.Open();
    const auto target = Record(0x3412, receiver.model.LocalNode());
    sender.model.Connect(tx, target.data(), 16);
    const std::vector<uint8_t> payload(10, 7);
    Check(sender.model.Send(tx, payload.data(), 10) == 492, "short send reports a full frame");
    std::vector<uint8_t> buffer(492, 0xFF);
    Check(receiver.model.Receive(rx, buffer.data(), 492) == 492, "frame is 492 bytes");
    Check(buffer[9] == 7 && buffer[10] == 0 && buffer[491] == 0, "deterministic zero padding");
}

void TestNodeIdIsByteSwappedForTheGuest() {
    InMemoryHub hub;
    Peer peer(hub, kHostA, 1);
    const auto node = peer.model.LocalNode();
    const auto eui = peer.model.LocalEuiWords();
    Check(eui.high == __builtin_bswap32(node.high) && eui.low == __builtin_bswap32(node.low),
          "guest compares record nodes with byte-swapped EUI words");
}

void TestInstancesOnOneHostAreDistinct() {
    InMemoryHub hub;
    Peer first(hub, kHostA, 0);
    Peer second(hub, kHostA, 1);
    Check(!(first.model.LocalNode() == second.model.LocalNode()), "same host, different instance, different node");
    const int rx = second.Open();
    const auto rx_record = Record(0x3412, IlinkNode{0, 0});
    Check(second.model.Bind(rx, rx_record.data(), 16) == 0, "instance port does not collide");
    const int rx_first = first.Open();
    Check(first.model.Bind(rx_first, rx_record.data(), 16) == 0, "both instances bind the same guest port");
    const int tx = first.Open();
    const auto target = Record(0x3412, second.model.LocalNode());
    first.model.Connect(tx, target.data(), 16);
    const auto payload = Frame(9);
    first.model.Send(tx, payload.data(), 492);
    std::vector<uint8_t> buffer(492);
    Check(second.model.Receive(rx, buffer.data(), 492) == 492, "instance routing reaches only the target");
    Check(first.model.Receive(rx_first, buffer.data(), 492) == 0, "other instance on the same host hears nothing");
}
}

int main() {
    TestAddressRecordRoundTrip();
    TestNodeMapping();
    TestSocketLifecycle();
    TestSizeRules();
    TestNoDataIsNotAPacket();
    TestLoopbackBetweenTwoModels();
    TestBroadcastReachesEveryBoundSocket();
    TestShortSendIsPaddedToFrame();
    TestNodeIdIsByteSwappedForTheGuest();
    TestInstancesOnOneHostAreDistinct();
    std::cout << "net_tests passed\n";
}
