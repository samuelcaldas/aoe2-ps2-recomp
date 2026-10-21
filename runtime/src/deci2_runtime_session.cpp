#include <aoe2/runtime/deci2_runtime_session.hpp>
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"

#include <iostream>
#include <mutex>
#include <unordered_map>

namespace aoe2::runtime {
namespace {
std::mutex binding_mutex;
std::unordered_map<PS2Runtime*, std::weak_ptr<Deci2Host>> bindings;
}

void write_game_tty(std::string_view text) {
    std::cerr << "[game-tty] " << escape_tty_text(text);
    std::cerr.flush();
    if (!std::cerr) throw std::ios_base::failure("game TTY stderr write failed");
}

Deci2RuntimeSession::Deci2RuntimeSession(PS2Runtime& runtime, Deci2Host::Sink sink)
    : runtime_(runtime) {
#if AOE2_GAME_TTY
    host_ = std::make_shared<Deci2Host>(sink ? std::move(sink) : write_game_tty);
    const std::lock_guard lock(binding_mutex);
    if (bindings.contains(&runtime)) throw Deci2Error("runtime already has a DECI2 session");
    bindings.emplace(&runtime, host_);
#endif
}

Deci2RuntimeSession::~Deci2RuntimeSession() { close(); }

void Deci2RuntimeSession::verify_output() const {
    if (host_) host_->verify_output();
}

void Deci2RuntimeSession::reset() {
    if (host_) host_->reset();
}

void Deci2RuntimeSession::close() {
    if (!host_) return;
    const std::lock_guard lock(binding_mutex);
    bindings.erase(&runtime_);
    host_->reset();
    host_.reset();
}

std::shared_ptr<Deci2Host> find_deci2_host(PS2Runtime& runtime) {
    const std::lock_guard lock(binding_mutex);
    const auto found = bindings.find(&runtime);
    if (found == bindings.end()) return {};
    return found->second.lock();
}

void invoke_deci2_handler(PS2Runtime& runtime, R5900Context& context,
                         std::shared_ptr<Deci2Host> host, Deci2Notification notification) {
    GuestInvocation invocation{};
    invocation.kind = GuestInvocationKind::HleCall;
    invocation.context = context;
    invocation.context.pc = notification.handler;
    SET_GPR_U32(&invocation.context, 4, static_cast<std::uint32_t>(notification.event));
    SET_GPR_U32(&invocation.context, 5, 0);
    SET_GPR_U32(&invocation.context, 6, notification.option);
    SET_GPR_U32(&invocation.context, 29, runtime.eeScheduler().invocationStackTop());
    SET_GPR_U32(&invocation.context, 31, 0);
    invocation.onComplete = [weak = std::weak_ptr(host), notification](const R5900Context&, R5900Context&) {
        if (const auto active = weak.lock()) active->finish(notification);
    };
    runtime.eeScheduler().invokeCurrent(std::move(invocation));
}
}
