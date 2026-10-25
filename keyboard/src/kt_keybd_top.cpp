// Origin: samuelcaldas/age-of-empires-2-decomp, kt_keybd.c,
// commit 945f15c56d02bed9de523870850f53784e5ad609. Binary-checked guest-memory port.
#include <aoe2/keyboard/kt_keybd_model.hpp>
#include <bit>

namespace aoe2::keyboard {
void KtKeybdModel::CopyBytes(uint32_t destination, uint32_t source, uint32_t size) {
    for (uint32_t index = 0; index < size; ++index)
        memory_.Store8(destination + index, memory_.Load8(source + index));
}
void KtKeybdModel::WritePacket(uint32_t entry, int kind, int modifier, uint32_t source, int size) {
    memory_.Store32(entry, 1);
    memory_.Store8(entry + 4, static_cast<uint8_t>(kind));
    memory_.Store8(entry + 5, static_cast<uint8_t>(modifier));
    // The original memcpy count is unsigned even though enqueue compares size as signed.
    CopyBytes(entry + 6, source, static_cast<uint32_t>(size));
    if (size < 14) Clear(entry + 6u + static_cast<uint32_t>(size), 14u - static_cast<uint32_t>(size));
}
int KtKeybdModel::PostPacket(int port, int kind, int modifier, uint32_t source, int size) {
    AssertPort(port, 0x251);
    if (size >= 14) memory_.ReportAssertion("kt_keybd.c", 0x252, "size < KT_KEYBD_KEY_MAX");
    if (port < 0 || !IsAllocated(Port(port))) return -1;
    const auto entry = WriteEntry(port);
    if (!entry || memory_.Load32(entry)) return 0;
    WritePacket(entry, kind, modifier, source, size);
    AdvanceWrite(port);
    return 0;
}
void KtKeybdModel::InitRecord(uint32_t address, int value) {
    memory_.Store32(address + 4, value);
    memory_.Store32(address + 8, 0xffffffff);
    memory_.Store32(address, 0xffffffff);
}
void KtKeybdModel::InitRecords(uint32_t address, int count, int stride) {
    for (int index = 0; index < count; ++index)
        InitRecord(address + static_cast<uint32_t>(index) * stride, index);
}
void KtKeybdModel::ReconcileRecords(uint32_t address, int count, int stride, int type) {
    for (int index = 0; index < count; ++index) {
        const auto record = address + static_cast<uint32_t>(index) * stride;
        const auto port = std::bit_cast<int32_t>(memory_.Load32(record + 8));
        if (port < 0 || Status(port) == type) continue;
        InitRecord(record, std::bit_cast<int32_t>(memory_.Load32(record + 4)));
    }
}
void KtKeybdModel::ApplyInfo(uint32_t address, int count, int stride, int type, uint32_t reply) {
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
void KtKeybdModel::InitProcessing(int count) {
    if (count >= 2) count = 1;
    Clear(0x50a380, 0x18);
    memory_.Store32(0x50a388, count);
    memory_.Store32(0x50a384, 0xffffffff);
    InitRecords(0x50a38c, 1, 0xc);
}
void KtKeybdModel::ProcessEvents() {
    const auto count = std::bit_cast<int32_t>(memory_.Load32(0x50a388));
    if (service_.Info(0x50a3c0) != 0) return;
    ReconcileRecords(0x50a38c, count, 0xc, 1);
    ApplyInfo(0x50a38c, count, 0xc, 1, 0x50a3c0);
    for (int index = 0; index < count; ++index) {
        const auto port = std::bit_cast<int32_t>(memory_.Load32(0x50a394u + index * 0xcu));
        ApplyRequests(index, port);
        if (!memory_.Load8(0x50a3c2u + index)) continue;
        if (service_.DeviceInfo(index, 0x50a440) != 0) continue;
        SetPayload(port, 0x50a440, 7);
        DrainEvents(index, port);
    }
}
void KtKeybdModel::ApplyRequests(int device, int port) {
    const int repeat = RepeatRequest(port);
    if (repeat < 0 && service_.SetLed(device, repeat & 0xff) == 0)
        ClearRepeatRequest(port, repeat & 0xff);
    const int led = LedRequest(port);
    if (led < 0 && service_.SetCapsType(device, led & 0xff) == 0)
        ClearLedRequest(port, led & 0xff);
}
void KtKeybdModel::DrainEvents(int device, int port) {
    while (service_.Read(device, 0x50a4c0) == 0) {
        const auto size = memory_.Load8(0x50a4c1);
        if (!size) return;
        const auto kind = memory_.Load8(0x50a4c0);
        const auto modifier = memory_.Load8(0x50a4c2);
        PostPacket(port, kind, modifier, 0x50a4c4, size);
        memory_.Store8(0x50a4c1, 0);
    }
}
}
