#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_stubs.h"
#include "runtime/ee_scheduler.h"
#include "synthetic_cdvd_disc.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace ps2_stubs { void resetSifState(); }
namespace {
constexpr uint32_t DESCRIPTOR = 0x4000, PACKET = 0x5000, ARGUMENTS = 0x6000;
constexpr uint32_t REPLY = 0x7000, RESULT = 0x8000, DESTINATION = 0x200000;
// Queue layout reference: samuelcaldas/age-of-empires-2-decomp, kt_libcdvd_server.c,
// eb2b12731694310da08d9b6501943169f99f4653. Entries below are synthetic requests.
constexpr uint32_t REQUEST_QUEUE = 0x4f1400, NCMD_SEMAPHORE = 0x4ed348;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void store(uint8_t* ram, uint32_t address, uint32_t value) { std::memcpy(ram + address, &value, 4); }
uint32_t load(uint8_t* ram, uint32_t address) {
    uint32_t value; std::memcpy(&value, ram + address, 4); return value;
}
void dma(PS2Runtime& runtime, uint32_t source, uint32_t destination, uint32_t bytes) {
    auto* ram = runtime.memory().getRDRAM();
    const std::array<uint32_t, 4> descriptor{source, destination, bytes, 0};
    std::memcpy(ram + DESCRIPTOR, descriptor.data(), sizeof(descriptor));
    R5900Context context{};
    SET_GPR_U32(&context, 4, DESCRIPTOR); SET_GPR_U32(&context, 5, 1);
    ps2_stubs::sceSifSetDma(ram, &context, &runtime);
    check(getRegU32(&context, 2) > 0, "RPC DMA failed");
}
template<std::size_t Count>
void command(PS2Runtime& runtime, uint32_t command_buffer, const std::array<uint32_t, Count>& words) {
    std::memcpy(runtime.memory().getRDRAM() + PACKET, words.data(), sizeof(words));
    dma(runtime, PACKET, command_buffer, sizeof(words));
}
uint32_t bind(PS2Runtime& runtime, uint32_t command_buffer) {
    command(runtime, command_buffer, std::array<uint32_t, 5>{20, 0, 0x80000002, 0, REPLY});
    command(runtime, command_buffer, std::array<uint32_t, 10>{40, 0, 0x80000009, 0, 1, 0, 1, 0x9000, 0x80000595, 0});
    auto* ram = runtime.memory().getRDRAM();
    check(load(ram, REPLY + 8) == 0x80000008, "SIF bind reply absent");
    const auto buffer = load(ram, REPLY + 40);
    R5900Context context{};
    ps2_stubs::sceSifSetDChain(ram, &context, &runtime);
    check(buffer != 0, "SIF RPC buffer not published");
    return buffer;
}
void read_rpc(PS2Runtime& runtime, uint32_t command_buffer, uint32_t rpc_buffer) {
    auto* ram = runtime.memory().getRDRAM();
    const std::array<uint32_t, 4> args{1, 2, DESTINATION, 0};
    std::memcpy(ram + ARGUMENTS, args.data(), sizeof(args));
    dma(runtime, ARGUMENTS, rpc_buffer, sizeof(args));
    command(runtime, command_buffer, std::array<uint32_t, 14>{
        56, 0, 0x8000000a, 0, 1, 0xa000, 2, 0x9000, 1, sizeof(args), RESULT, 4, 1,
        0x80000595u ^ 0xacacacacu});
}
int initialize(PS2Runtime& runtime, const SyntheticCdvdDisc& disc) {
    check(runtime.memory().initialize() && runtime.syncCoreSubsystems(), "runtime initialization");
    auto paths = PS2Runtime::getIoPaths(); paths.cdImage = disc.root / "aoe2.iso";
    PS2Runtime::setIoPaths(paths); ps2_stubs::resetSifState();
    auto* ram = runtime.memory().getRDRAM();
    R5900Context context{}; runtime.eeScheduler().reset(ram, context);
    const int id = runtime.eeScheduler().createSemaphore(0, 4, 0, 0);
    check(id > 0, "request semaphore creation"); store(ram, NCMD_SEMAPHORE, uint32_t(id));
    return id;
}
std::array<uint8_t, 0x108> seed_requests(uint8_t* ram, unsigned occupied) {
    for (unsigned index = 0; index < occupied; ++index) {
        store(ram, REQUEST_QUEUE + index * 0x38, 1);
        store(ram, REQUEST_QUEUE + index * 0x38 + 0x14, 0x300000 + index * 0x80);
    }
    std::array<uint8_t, 0x108> before{};
    std::memcpy(before.data(), ram + REQUEST_QUEUE, before.size());
    return before;
}
void bind_and_read(PS2Runtime& runtime) {
    R5900Context context{};
    SET_GPR_U32(&context, 4, 2);
    ps2_stubs::sceSifGetReg(runtime.memory().getRDRAM(), &context, &runtime);
    const auto command_buffer = getRegU32(&context, 2);
    const auto rpc_buffer = bind(runtime, command_buffer);
    read_rpc(runtime, command_buffer, rpc_buffer);
}
void read_completion_does_not_invent_guest_requests(unsigned occupied) {
    SyntheticCdvdDisc disc; PS2Runtime runtime;
    const int id = initialize(runtime, disc);
    auto* ram = runtime.memory().getRDRAM();
    const auto before = seed_requests(ram, occupied);
    bind_and_read(runtime);
    check(std::equal(disc.bytes.begin() + 2048, disc.bytes.begin() + 6144, ram + DESTINATION), "real SIF read bytes differ");
    check(std::equal(before.begin(), before.end(), ram + REQUEST_QUEUE), "SIF completion invented or corrupted a guest request");
    check(runtime.eeScheduler().semaphore(id)->count == 0, "SIF completion signaled guest request semaphore");
    check(load(ram, REPLY + 8) == 0x80000008 && load(ram, REPLY + 32) == 0x8000000a, "read RPC completion not delivered");
}
}
int main() {
    try {
        for (unsigned occupied : {0u, 1u, 4u}) read_completion_does_not_invent_guest_requests(occupied);
        std::cout << "CDVD RPC read bytes/completion preserve guest request ownership\n";
        return 0;
    } catch (const std::runtime_error& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
