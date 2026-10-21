#include <aoe2/runtime/deci2_runtime_session.hpp>
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"
#include "runtime/ee_scheduler.h"

#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace aoe2::runtime;
void verify_deci2_session_lifetime();
extern PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[];
namespace {
constexpr std::uint32_t ENTRY = 4096, HANDLER = 8192, ARGUMENTS = 12288, BUFFER = 16384;
int socket_id = 0, polls = 0;
bool failed_sink = false;
std::vector<unsigned> events;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

int call(PS2Runtime& runtime, R5900Context& context, int operation,
         std::initializer_list<std::uint32_t> words) {
    std::memcpy(runtime.memory().getRDRAM() + ARGUMENTS, words.begin(), words.size() * sizeof(std::uint32_t));
    SET_GPR_U32(&context, 4, operation);
    SET_GPR_U32(&context, 5, ARGUMENTS);
    ps2_syscalls::dispatchNumericSyscall(124, runtime.memory().getRDRAM(), &context, &runtime);
    return static_cast<int>(getRegU32(&context, 2));
}

void handler(std::uint8_t*, R5900Context* context, PS2Runtime* runtime) {
    const auto event = getRegU32(context, 4);
    require(getRegU32(context, 5) == 0, "wrong event parameter");
    require(getRegU32(context, 6) == 1234, "workspace used instead of opt");
    events.push_back(event);
    if (event == 3) require(call(*runtime, *context, -6, {unsigned(socket_id), BUFFER, 15}) ==
        (failed_sink ? -1 : 15), "wrong send result");
    context->pc = getRegU32(context, 31);
}

void entry(std::uint8_t*, R5900Context* context, PS2Runtime* runtime) {
    if (polls++ < 4) {
        context->pc = ENTRY;
        call(*runtime, *context, 4, {unsigned(socket_id)});
        return;
    }
    require(getRegU32(context, 28) == 321, "guest GP corrupted");
    require(getRegU32(context, 29) == 1048576, "guest stack corrupted");
    context->pc = 0;
    runtime->eeScheduler().requestStop();
}

void write_packet(PS2Runtime& runtime) {
    const std::uint8_t bytes[] = {15, 0, 0, 0, 16, 2, 'E', 'H', 0, 0, 0, 0, 'o', 'k', '\n'};
    std::memcpy(runtime.memory().getRDRAM() + BUFFER, bytes, sizeof(bytes));
}

void exercises_scheduler_callbacks(bool fail_sink) {
    failed_sink = fail_sink;
    events.clear();
    polls = 0;
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    require(runtime.syncCoreSubsystems(), "subsystem initialization failed");
    g_ps2RecompiledFunctionTable[ENTRY / 4] = entry;
    g_ps2RecompiledFunctionTable[HANDLER / 4] = handler;
    std::string output;
    Deci2RuntimeSession session(runtime, [&](std::string_view text) {
        if (fail_sink) throw std::ios_base::failure("synthetic output failure");
        output += text;
    });
    R5900Context context{};
    socket_id = call(runtime, context, 1, {EE_TTY_PROTOCOL, 1234, HANDLER, BUFFER + 128});
#if AOE2_GAME_TTY
    require(socket_id > 0, "TTY open failed");
    require(call(runtime, context, 3, {unsigned(socket_id), 'H'}) == 0, "request failed");
    write_packet(runtime);
    context.pc = ENTRY;
    SET_GPR_U32(&context, 28, 321);
    SET_GPR_U32(&context, 29, 1048576);
    runtime.eeScheduler().reset(runtime.memory().getRDRAM(), context);
    runtime.eeScheduler().run();
    if (!fail_sink) {
        require(events == std::vector<unsigned>({3, 4}), "wrong event sequence");
        require(output == "ok\n", "TTY output missing or duplicated");
        session.verify_output();
    }
    if (fail_sink) {
        require(events == std::vector<unsigned>({3}), "failed send delivered success event");
        bool rejected = false;
        try { session.verify_output(); }
        catch (const Deci2Error&) { rejected = true; }
        require(rejected && output.empty() && polls < 4, "sink failure silently accepted");
    }
#else
    require(socket_id == -1, "disabled TTY opened a socket");
#endif
    session.reset();
    require(call(runtime, context, 4, {unsigned(socket_id)}) == -1, "reset kept old socket");
    SET_GPR_U32(&context, 4, 1);
    SET_GPR_U32(&context, 5, 33554428);
    ps2_syscalls::Deci2Call(runtime.memory().getRDRAM(), &context, &runtime);
    require(static_cast<int>(getRegU32(&context, 2)) == -1, "out of bounds arguments accepted");
    session.close();
}

void rejects_unbound_runtime() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    R5900Context context{};
    require(call(runtime, context, 1, {EE_TTY_PROTOCOL, 1234, HANDLER, BUFFER}) == -1,
            "unbound runtime opened TTY");
}
}

int main() {
    try {
        verify_deci2_session_lifetime();
        rejects_unbound_runtime();
        exercises_scheduler_callbacks(false);
        exercises_scheduler_callbacks(true);
        std::cout << "DECI2 runtime tests passed\n";
        std::cout.flush();
        std::_Exit(0);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        std::cerr.flush();
        std::_Exit(1);
    }
}
