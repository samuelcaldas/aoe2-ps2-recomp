#include <aoe2/runtime/deci2_runtime_session.hpp>
#include "Common.h"

#include <array>
#include <span>

namespace {
using namespace aoe2::runtime;
using Arguments = std::array<std::uint32_t, 4>;

std::span<const std::uint8_t> guest_bytes(std::uint8_t* memory, std::uint32_t address, std::size_t length) {
    std::uint32_t offset = 0;
    bool scratch = false;
    if (!memory || !resolveEeGuestRange(address, length, offset, scratch) || scratch)
        throw Deci2Error("invalid DECI2 RAM range");
    return {memory + offset, length};
}

std::size_t argument_count(int operation) {
    if (operation == 1) return 4;
    if (operation == -5 || operation == -6) return 3;
    if (operation == 3 || operation == -7) return 2;
    return 1;
}

Arguments read_arguments(std::uint8_t* memory, std::uint32_t address, int operation) {
    const auto bytes = guest_bytes(memory, address, argument_count(operation) * 4);
    Arguments words{};
    std::memcpy(words.data(), bytes.data(), bytes.size());
    return words;
}

int open_channel(PS2Runtime& runtime, Deci2Host& host, std::uint8_t* memory, const Arguments& words) {
    if (words[0] > 65535 || !runtime.hasFunction(words[2])) throw Deci2Error("invalid DECI2 handler");
    guest_bytes(memory, words[3], 56);
    return host.open(words[0], words[2], words[1]);
}

int send_packet(Deci2Host& host, std::uint8_t* memory, const Arguments& words) {
    if (words[2] > 65535) throw Deci2Error("invalid DECI2 transfer length");
    return static_cast<int>(host.send(words[0], guest_bytes(memory, words[1], words[2])));
}

int kernel_puts(std::uint8_t* memory, std::uint32_t address) {
    std::string text;
    for (std::uint32_t offset = 0; offset < 4096; ++offset) {
        if (address > UINT32_MAX - offset) throw Deci2Error("DECI2 string address overflow");
        const auto byte = guest_bytes(memory, address + offset, 1).front();
        if (byte == 0) { write_game_tty(text); return static_cast<int>(text.size()); }
        text.push_back(static_cast<char>(byte));
    }
    throw Deci2Error("unterminated DECI2 string");
}

int perform_operation(PS2Runtime& runtime, Deci2Host& host, std::uint8_t* memory,
                      int operation, const Arguments& words) {
    if (operation == 1) return open_channel(runtime, host, memory, words);
    if (operation == 2) { host.close(words[0]); return 0; }
    if (operation == 3 || operation == -7) { host.request(words[0], words[1]); return 0; }
    if (operation == -6) return send_packet(host, memory, words);
    if (operation == -8 || operation == -9) { host.lock(words[0], operation == -8); return 0; }
    if (operation == 16) return kernel_puts(memory, words[0]);
    if (operation == -5) { host.check(words[0]); guest_bytes(memory, words[1], words[2]); return 0; }
    throw Deci2Error("unsupported DECI2 operation");
}

void dispatch_operation(PS2Runtime& runtime, std::shared_ptr<Deci2Host> host,
                        std::uint8_t* memory, R5900Context& context) {
    const auto operation = static_cast<int>(getRegU32(&context, 4));
    const auto words = read_arguments(memory, getRegU32(&context, 5), operation);
    if (operation != 4) {
        setReturnS32(&context, perform_operation(runtime, *host, memory, operation, words));
        return;
    }
    const auto event = host->poll(words[0]);
    setReturnS32(&context, 0);
    if (event && event->event == Deci2Event::Error) {
        runtime.eeScheduler().requestStop();
        throw Deci2Error("DECI2 transfer failed; stopping instead of polling forever");
    }
    if (event) invoke_deci2_handler(runtime, context, std::move(host), *event);
}
}

namespace ps2_syscalls {
void Deci2Call(std::uint8_t* memory, R5900Context* context, PS2Runtime* runtime) {
    setReturnS32(context, -1);
    if (!runtime) return;
    const auto host = aoe2::runtime::find_deci2_host(*runtime);
    if (!host) return;
    try {
        dispatch_operation(*runtime, host, memory, *context);
    } catch (const aoe2::runtime::Deci2Error& error) {
        std::cerr << "[game-tty:error] " << error.what() << '\n';
        setReturnS32(context, -1);
    }
}
}
