// Origin: samuelcaldas/age-of-empires-2-decomp, kt_mouse.c,
// commit 945f15c56d02bed9de523870850f53784e5ad609. Binary-checked native guest-memory port.
#include <aoe2/mouse/kt_mouse_model.hpp>
#include <bit>

namespace aoe2::mouse {
int KtMouseModel::PostEvent(int port, int buttons, int dx, int dy, int wheel) {
    AssertPort(port, 0x293);
    if (port < 0 || !IsAllocated(Port(port))) return -1;
    const auto entry = WriteEntry(port);
    if (!entry || memory_.Load32(entry)) return 0;
    memory_.Store32(entry, 1);
    memory_.Store32(entry + 4, buttons);
    memory_.Store32(entry + 8, dx);
    memory_.Store32(entry + 12, dy);
    memory_.Store32(entry + 16, wheel);
    AdvanceWrite(port);
    return 0;
}
void KtMouseModel::InitRecord(uint32_t address, int value) {
    memory_.Store32(address + 4, value);
    memory_.Store32(address + 8, 0xffffffff);
    memory_.Store32(address, 0xffffffff);
}
void KtMouseModel::InitRecords(uint32_t address, int count, int stride) {
    for (int index = 0; index < count; ++index)
        InitRecord(address + static_cast<uint32_t>(index) * stride, index);
}
void KtMouseModel::ReconcileRecords(uint32_t address, int count, int stride, int type) {
    for (int index = 0; index < count; ++index) {
        const auto record = address + static_cast<uint32_t>(index) * stride;
        const auto port = std::bit_cast<int32_t>(memory_.Load32(record + 8));
        if (port < 0 || Status(port) == type) continue;
        InitRecord(record, std::bit_cast<int32_t>(memory_.Load32(record + 4)));
    }
}
void KtMouseModel::ApplyInfo(uint32_t address, int count, int stride, int type, uint32_t reply) {
    const int available = std::bit_cast<int8_t>(memory_.Load8(reply));
    if (available < count) count = available;
    for (int index = 0; index < count; ++index) {
        const auto record = address + static_cast<uint32_t>(index) * stride;
        auto port = std::bit_cast<int32_t>(memory_.Load32(record + 8));
        const auto connected = memory_.Load8(reply + index + 2) != 0;
        if (connected && port < 0) port = Allocate(type);
        if (connected) memory_.Store32(record + 8, port);
        SetState(port, connected ? 1 : 0);
        memory_.Store32(record, connected ? 1 : 0);
    }
}
void KtMouseModel::InitProcessing(int count) {
    if (count >= 2) count = 1;
    Clear(0x50a080, 0x18);
    memory_.Store32(0x50a088, count);
    memory_.Store32(0x50a084, 0xffffffff);
    InitRecords(0x50a08c, 1, 0xc);
}
void KtMouseModel::ProcessEvents() {
    const auto count = std::bit_cast<int32_t>(memory_.Load32(0x50a088));
    if (service_.Info(0x50a0c0) != 0) return;
    ReconcileRecords(0x50a08c, count, 0xc, 1);
    ApplyInfo(0x50a08c, count, 0xc, 1, 0x50a0c0);
    for (int index = 0; index < count; ++index) {
        const auto port = std::bit_cast<int32_t>(memory_.Load32(0x50a094u + index * 0xcu));
        if (!memory_.Load8(0x50a0c2u + index)) continue;
        if (service_.DeviceInfo(index, 0x50a140) != 0) continue;
        SetPayload(port, 0x50a140, 7);
        DrainEvents(index, port);
    }
}
void KtMouseModel::DrainEvents(int device, int port) {
    while (service_.Read(device, 0x50a1c0) == 0) {
        const int size = std::bit_cast<int8_t>(memory_.Load8(0x50a1c0));
        if (!size) return;
        const int buttons = std::bit_cast<int8_t>(memory_.Load8(0x50a1c1));
        const int dx = std::bit_cast<int8_t>(memory_.Load8(0x50a1c2));
        const int dy = std::bit_cast<int8_t>(memory_.Load8(0x50a1c3));
        const int wheel = size < 4 ? 0 : std::bit_cast<int8_t>(memory_.Load8(0x50a1c4));
        PostEvent(port, buttons, dx, dy, wheel);
        memory_.Store8(0x50a1c0, 0);
    }
}
}
