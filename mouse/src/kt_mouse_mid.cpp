// Origin: samuelcaldas/age-of-empires-2-decomp, kt_mouse.c,
// commit 945f15c56d02bed9de523870850f53784e5ad609. Binary-checked native guest-memory port.
#include <aoe2/mouse/kt_mouse_model.hpp>
#include <bit>

namespace aoe2::mouse {
int KtMouseModel::ReadPacket(int port, uint32_t output) {
    const auto entry = ReadEntry(port);
    if (!entry || !memory_.Load32(entry)) return 0;
    for (uint32_t index = 0; index < 4; ++index)
        memory_.Store32(output + index * 4, memory_.Load32(entry + 4 + index * 4));
    memory_.Store32(entry, 0);
    AdvanceRead(port);
    return 1;
}
int KtMouseModel::ReadPayload(int port, uint32_t output) {
    if (!output) return 0;
    AssertPort(port, 0x19d);
    const auto address = Port(port);
    const auto type = PortType(address);
    const int size = static_cast<uint32_t>(type) - 1u < 3u
        ? std::bit_cast<int32_t>(memory_.Load32(address + 8)) : 0;
    for (int index = 0; index < size; ++index)
        memory_.Store8(output + 4u + index, memory_.Load8(address + 0xcu + index));
    memory_.Store32(output, static_cast<uint32_t>(type));
    return size;
}
int KtMouseModel::ResetPorts(int force) {
    int reset = 0;
    const auto count = std::bit_cast<int32_t>(memory_.Load32(0x50a064));
    for (int index = 0; index < count; ++index) {
        if (!force && PortState(Port(index)) != 0) continue;
        memory_.Store16(Port(index) + 2, 0xffff);
        ++reset;
    }
    return reset;
}
uint32_t KtMouseModel::FindFreePort() {
    const auto count = std::bit_cast<int32_t>(memory_.Load32(0x50a064));
    for (int index = 0; index < count; ++index) {
        if (PortState(Port(index)) == -1) return Port(index);
    }
    return 0;
}
void KtMouseModel::ResetQueue(uint32_t address) {
    if (!address) return;
    Clear(address + 0xc, 0x90);
    memory_.Store32(address + 8, 0);
    memory_.Store32(address + 4, 0);
}
int KtMouseModel::Allocate(int type) {
    const auto address = FindFreePort();
    if (!address) return -1;
    memory_.Store16(address + 2, 0);
    memory_.Store32(address + 4, static_cast<uint32_t>(type));
    ResetQueue(address + 0x14);
    return std::bit_cast<int16_t>(memory_.Load16(address));
}
void KtMouseModel::Release(int port, int type) {
    AssertPort(port, 0x23b);
    const auto address = Port(port);
    if (PortType(address) != type) return;
    memory_.Store32(address + 4, 0);
    memory_.Store16(address + 2, 0xffff);
    ResetQueue(address + 0x14);
}
int KtMouseModel::SetState(int port, int state) {
    AssertPort(port, 0x259);
    if (port < 0 || !IsAllocated(Port(port))) return -1;
    memory_.Store16(Port(port) + 2, static_cast<uint16_t>(state));
    return 0;
}
int KtMouseModel::SetPayload(int port, uint32_t source, int size) {
    AssertPort(port, 0x276);
    if (port < 0 || !IsAllocated(Port(port))) return -1;
    if (static_cast<uint32_t>(size) >= 9) size = 8;
    const auto address = Port(port);
    for (int index = 0; index < size; ++index)
        memory_.Store8(address + 0xcu + index, memory_.Load8(source + index));
    memory_.Store32(address + 8, size);
    return 0;
}
}
