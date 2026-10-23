// KCET i.LINK socket replacements: the game's socket wrappers are answered by the
// host socket model instead of the IOP service, which has no FireWire hardware.
#include <aoe2/game_hooks/kcet_handlers.hpp>
#include <aoe2/net/ilink_socket_model.hpp>
#include <aoe2/net/null_transport.hpp>
#ifndef __SWITCH__
#include <aoe2/platform/desktop/udp_transport.hpp>
#endif
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>

namespace {
using namespace aoe2::net;
constexpr int kRegA0 = 4;
constexpr int kRegA1 = 5;
constexpr int kRegA2 = 6;
constexpr int kRegT0 = 8;
constexpr int kRegT1 = 9;
constexpr int kRegRa = 31;
constexpr uint32_t kNodeIdBytes = 8;
// A real receive is an RPC round trip to the IOP, during which the calling thread sleeps.
// About 1 ms of EE time keeps polling from starving lower-priority guest threads.
constexpr uint64_t kNoPacketLatencyCycles = EeScheduler::kEeClockHz / 1000;

std::mutex g_net_mutex;

struct NetworkState {
    std::unique_ptr<ITransportFactory> factory;
    std::unique_ptr<IlinkSocketModel> model;
};

NetworkState MakeState() {
    NetworkState state;
#ifdef __SWITCH__
    state.factory = std::make_unique<NullTransportFactory>();
    state.model = std::make_unique<IlinkSocketModel>(*state.factory, NodeFor(0, 0));
#else
    const uint32_t local_ipv4 = aoe2::platform::DetectLocalIpv4();
    state.factory = std::make_unique<aoe2::platform::UdpTransportFactory>(local_ipv4);
    state.model = std::make_unique<IlinkSocketModel>(*state.factory, NodeFor(local_ipv4, aoe2::platform::InstanceFromEnvironment()));
#endif
    return state;
}

IlinkSocketModel& Model() {
    static NetworkState state = MakeState();
    return *state.model;
}

void ReturnToCaller(R5900Context* ctx, int value) {
    setReturnU32(ctx, static_cast<uint32_t>(value));
    ctx->pc = getRegU32(ctx, kRegRa);
}

bool ReadGuest(uint8_t* rdram, uint32_t address, uint8_t* out, std::size_t size) {
    const uint8_t* source = getMemPtr(rdram, address);
    if (source == nullptr) return false;
    std::memcpy(out, source, size);
    return true;
}

bool WriteGuest(uint8_t* rdram, uint32_t address, const uint8_t* data, std::size_t size) {
    uint8_t* destination = getMemPtr(rdram, address);
    if (destination == nullptr) return false;
    std::memcpy(destination, data, size);
    return true;
}

// Returns 0 to the caller after a modelled device latency, letting other threads run.
// The scheduler writes 0 into v0 when the sleeping thread is woken.
void SleepForDeviceLatency(R5900Context* ctx, PS2Runtime* runtime) {
    EeScheduler& scheduler = runtime->eeScheduler();
    if (!scheduler.isExecutingGuest()) return;
    ctx->pc = getRegU32(ctx, kRegRa);
    const int thread = scheduler.currentThreadId();
    scheduler.scheduleHostCallback(kNoPacketLatencyCycles, [&scheduler, thread]() {
        scheduler.wakeupThread(thread, true);
    });
    scheduler.sleepCurrent();
}

int AsInt(R5900Context* ctx, int reg) { return static_cast<int>(getRegU32(ctx, reg)); }

void NetOpen(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_net_mutex);
    ReturnToCaller(ctx, Model().Open(AsInt(ctx, kRegA0), AsInt(ctx, kRegA1), AsInt(ctx, kRegA2)));
}

using AddressCall = int (IlinkSocketModel::*)(int, const uint8_t*, int);

void CallWithAddress(uint8_t* rdram, R5900Context* ctx, AddressCall call) {
    std::lock_guard<std::mutex> lock(g_net_mutex);
    const int length = AsInt(ctx, kRegA2);
    std::array<uint8_t, kIlinkAddressLength> record{};
    const std::size_t readable = static_cast<std::size_t>(std::clamp(length, 0, int{kIlinkAddressLength}));
    if (!ReadGuest(rdram, getRegU32(ctx, kRegA1), record.data(), readable))
        return ReturnToCaller(ctx, kErrorFailure);
    ReturnToCaller(ctx, (Model().*call)(AsInt(ctx, kRegA0), record.data(), length));
}

void NetBind(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { CallWithAddress(rdram, ctx, &IlinkSocketModel::Bind); }
void NetConnect(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { CallWithAddress(rdram, ctx, &IlinkSocketModel::Connect); }

void NetSend(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_net_mutex);
    const int length = AsInt(ctx, kRegA2);
    std::array<uint8_t, kFrameBytes> data{};
    const std::size_t readable = static_cast<std::size_t>(std::clamp(length, 0, int{kFrameBytes}));
    if (!ReadGuest(rdram, getRegU32(ctx, kRegA1), data.data(), readable))
        return ReturnToCaller(ctx, kErrorFailure);
    ReturnToCaller(ctx, Model().Send(AsInt(ctx, kRegA0), data.data(), length));
}

void CopyReceived(uint8_t* rdram, uint32_t address, const std::array<uint8_t, kFrameBytes>& data, int count, int capacity) {
    if (count <= 0 || count > capacity) return;
    WriteGuest(rdram, address, data.data(), static_cast<std::size_t>(count));
}

int ReceiveLocked(uint8_t* rdram, R5900Context* ctx) {
    std::lock_guard<std::mutex> lock(g_net_mutex);
    const int capacity = AsInt(ctx, kRegA2);
    std::array<uint8_t, kFrameBytes> data{};
    const int count = Model().Receive(AsInt(ctx, kRegA0), data.data(), capacity);
    CopyReceived(rdram, getRegU32(ctx, kRegA1), data, count, capacity);
    ReturnToCaller(ctx, count);
    return count;
}

void NetRecv(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    if (ReceiveLocked(rdram, ctx) == 0) SleepForDeviceLatency(ctx, runtime);
}

int ReceiveFromLocked(uint8_t* rdram, R5900Context* ctx) {
    std::lock_guard<std::mutex> lock(g_net_mutex);
    const uint32_t length_pointer = getRegU32(ctx, kRegT1);
    uint32_t address_length = 0;
    if (!ReadGuest(rdram, length_pointer, reinterpret_cast<uint8_t*>(&address_length), sizeof(address_length)))
        return ReturnToCaller(ctx, kErrorFailure), kErrorFailure;
    if (address_length != kIlinkAddressLength) return ReturnToCaller(ctx, kErrorInvalidArgument), kErrorInvalidArgument;
    const int capacity = AsInt(ctx, kRegA2);
    std::array<uint8_t, kFrameBytes> data{};
    IlinkAddress source;
    const int count = Model().ReceiveFrom(AsInt(ctx, kRegA0), data.data(), capacity, source);
    CopyReceived(rdram, getRegU32(ctx, kRegA1), data, count, capacity);
    if (count > 0) {
        const auto record = SerializeIlinkAddress(source);
        WriteGuest(rdram, getRegU32(ctx, kRegT0), record.data(), record.size());
        WriteGuest(rdram, length_pointer, reinterpret_cast<const uint8_t*>(&address_length), sizeof(address_length));
    }
    ReturnToCaller(ctx, count);
    return count;
}

void NetRecvFrom(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    if (ReceiveFromLocked(rdram, ctx) == 0) SleepForDeviceLatency(ctx, runtime);
}

void NetNodeId(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_net_mutex);
    const IlinkNode eui = Model().LocalEuiWords();
    const uint32_t words[2] = {eui.high, eui.low};
    if (!WriteGuest(rdram, getRegU32(ctx, kRegA0), reinterpret_cast<const uint8_t*>(words), kNodeIdBytes))
        return ReturnToCaller(ctx, kErrorFailure);
    ReturnToCaller(ctx, 0);
}
} // namespace

namespace aoe2::game_hooks {
void RegisterNetworkHandlers(KcetHandlers& handlers) {
    handlers.emplace("kcet.net.open", NetOpen);
    handlers.emplace("kcet.net.bind", NetBind);
    handlers.emplace("kcet.net.connect", NetConnect);
    handlers.emplace("kcet.net.send", NetSend);
    handlers.emplace("kcet.net.recv", NetRecv);
    handlers.emplace("kcet.net.recvfrom", NetRecvFrom);
    handlers.emplace("kcet.net.node_id", NetNodeId);
}
}
