#include <aoe2/runtime/deci2_runtime_session.hpp>
#include "ps2_runtime.h"

#include <stdexcept>

using namespace aoe2::runtime;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

void verify_deci2_session_lifetime() {
    PS2Runtime first;
    PS2Runtime second;
    Deci2RuntimeSession first_session(first, [](std::string_view) {});
    Deci2RuntimeSession second_session(second, [](std::string_view) {});
#if AOE2_GAME_TTY
    auto host = find_deci2_host(first);
    const auto other = find_deci2_host(second);
    require(host && other && host != other, "runtime transports not isolated");
    const auto socket = host->open(EE_TTY_PROTOCOL, 1, 2);
    host->request(socket, 'H');
    const auto stale = *host->poll(socket);
    std::weak_ptr<Deci2Host> retired = host;
    host.reset();
    first_session.close();
    require(retired.expired(), "closed runtime retained its transport");
    require(!find_deci2_host(first), "closed runtime still bound");
    require(find_deci2_host(second) == other, "closing one runtime unbound another");
    Deci2RuntimeSession replacement(first, [](std::string_view) {});
    const auto fresh = find_deci2_host(first);
    const auto fresh_socket = fresh->open(EE_TTY_PROTOCOL, 1, 2);
    fresh->request(fresh_socket, 'H');
    if (const auto active = retired.lock()) active->finish(stale);
    require(fresh->poll(fresh_socket).has_value(), "retired notification corrupted replacement");
    bool rejected = false;
    try { Deci2RuntimeSession duplicate(first); }
    catch (const Deci2Error&) { rejected = true; }
    require(rejected, "duplicate runtime binding accepted");
#endif
    first_session.close();
    second_session.close();
}
