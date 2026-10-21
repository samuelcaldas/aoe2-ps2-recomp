#include <aoe2/runtime/deci2_host.hpp>
#include "deci2_tty_packet.hpp"

#include <climits>
#include <iostream>
#include <unordered_map>
#include <utility>

namespace aoe2::runtime {
namespace {
enum class Phase { Idle, Requested, Writing, Accepted, Captured, Completing, Failed, ReportingError };
struct Channel {
    std::uint32_t handler;
    std::uint32_t option;
    std::uint64_t generation;
    Phase phase = Phase::Idle;
    Deci2TtyPacket packet{};
    std::size_t accepted = 0;
    std::size_t before_callback = 0;
    bool locked = false;
};

Deci2Notification notification(int socket, const Channel& channel, Deci2Event event) {
    return {socket, channel.generation, event, channel.handler, channel.option};
}
}

struct Deci2Host::State {
    std::unordered_map<int, Channel> channels;
    int next_socket = 1;
    std::uint64_t generation = 0;
    bool failed = false;

    Channel& require(int socket) {
        const auto found = channels.find(socket);
        if (found == channels.end()) throw Deci2Error("unknown DECI2 socket");
        return found->second;
    }
};

Deci2Host::Deci2Host(Sink sink) : state_(std::make_unique<State>()), sink_(std::move(sink)) {
    if (!sink_) throw Deci2Error("DECI2 output sink is required");
}
Deci2Host::~Deci2Host() = default;

int Deci2Host::open(std::uint16_t protocol, std::uint32_t handler, std::uint32_t option) {
    if (protocol != EE_TTY_PROTOCOL || handler == 0) throw Deci2Error("unsupported DECI2 channel");
    if (state_->channels.size() >= 32 || state_->next_socket == INT_MAX)
        throw Deci2Error("DECI2 channel limit reached");
    const auto socket = state_->next_socket++;
    state_->channels.emplace(socket, Channel{handler, option, ++state_->generation});
    return socket;
}

void Deci2Host::close(int socket) {
    state_->require(socket);
    state_->channels.erase(socket);
}

void Deci2Host::request(int socket, std::uint32_t destination) {
    auto& channel = state_->require(socket);
    if (destination != 'H') throw Deci2Error("unsupported DECI2 destination");
    if (channel.phase != Phase::Idle) throw Deci2Error("DECI2 send already pending");
    channel.packet.clear();
    channel.accepted = 0;
    channel.phase = Phase::Requested;
    channel.generation = ++state_->generation;
}

std::optional<Deci2Notification> Deci2Host::poll(int socket) {
    auto& channel = state_->require(socket);
    if (channel.locked) return std::nullopt;
    if (channel.phase == Phase::Requested) {
        channel.phase = Phase::Writing;
        channel.before_callback = channel.accepted;
        return notification(socket, channel, Deci2Event::Write);
    }
    if (channel.phase == Phase::Captured) {
        channel.phase = Phase::Completing;
        return notification(socket, channel, Deci2Event::WriteDone);
    }
    if (channel.phase != Phase::Failed) return std::nullopt;
    channel.phase = Phase::ReportingError;
    return notification(socket, channel, Deci2Event::Error);
}

std::size_t Deci2Host::send(int socket, std::span<const std::uint8_t> bytes) {
    auto& channel = state_->require(socket);
    if (channel.phase != Phase::Writing) throw Deci2Error("DECI2 write outside handler");
    try {
        const auto text = channel.packet.append(bytes);
        if (text) sink_(*text);
        channel.accepted += bytes.size();
        if (text) channel.phase = Phase::Accepted;
        return bytes.size();
    } catch (const std::ios_base::failure& error) {
        channel.phase = Phase::Failed;
        state_->failed = true;
        std::cerr << "[game-tty:error] output failed: " << error.what() << '\n';
        throw Deci2Error("DECI2 output failed");
    } catch (const Deci2Error&) {
        channel.phase = Phase::Failed;
        state_->failed = true;
        throw;
    }
}

void Deci2Host::finish(const Deci2Notification& completed) {
    const auto found = state_->channels.find(completed.socket);
    if (found == state_->channels.end()) return;
    auto& channel = found->second;
    if (channel.generation != completed.generation) return;
    if (completed.event == Deci2Event::Error) { close(completed.socket); return; }
    if (completed.event == Deci2Event::WriteDone) { channel.phase = Phase::Idle; return; }
    if (channel.phase == Phase::Accepted) { channel.phase = Phase::Captured; return; }
    if (channel.phase != Phase::Writing) return;
    channel.phase = Phase::Requested;
    if (channel.accepted == channel.before_callback) fail(completed.socket);
}

void Deci2Host::lock(int socket, bool locked) { state_->require(socket).locked = locked; }
void Deci2Host::check(int socket) const { state_->require(socket); }
void Deci2Host::fail(int socket) {
    state_->require(socket).phase = Phase::Failed;
    state_->failed = true;
}
void Deci2Host::reset() {
    state_->channels.clear();
    state_->failed = false;
}
void Deci2Host::verify_output() const {
    if (state_->failed) throw Deci2Error("DECI2 output did not complete successfully");
    for (const auto& [socket, channel] : state_->channels) {
        if (channel.phase != Phase::Idle) throw Deci2Error("DECI2 transfer still pending at shutdown");
    }
}
}
