// Origin: samuelcaldas/age-of-empires-2-decomp, kt_keybd.c,
// commit 945f15c56d02bed9de523870850f53784e5ad609. Binary-checked guest-memory port.
#include <aoe2/keyboard/kt_keybd_model.hpp>
#include <bit>

namespace aoe2::keyboard {
KtKeybdModel::KtKeybdModel(runtime::GuestAccess& memory, KeyboardRpc& service)
    : memory_(memory), service_(service) {}
// These are actual alignment returns in the owned ELF, not hardware stubs.
void KtKeybdModel::WorkPadding() {}
void KtKeybdModel::DevicePadding() {}
void KtKeybdModel::Clear(uint32_t address, uint32_t size) {
    for (uint32_t index = 0; index < size; ++index) memory_.Store8(address + index, 0);
}
uint32_t KtKeybdModel::Port(int port) {
    return memory_.Load32(0x50a340) + static_cast<uint32_t>(port) * 0x78u;
}
void KtKeybdModel::AssertPort(int port, int line) {
    if (port < std::bit_cast<int32_t>(memory_.Load32(0x50a33c))) return;
    const auto expression = line < 0x1f9 ? "keybd_number < W->n_keybd" : "number < W->n_keybd";
    memory_.ReportAssertion("kt_keybd.c", line, expression);
}
void KtKeybdModel::Init(int count) {
    if (count >= 2) memory_.ReportAssertion("kt_keybd.c", 0x66, "n_keybd <= KT_KEYBD_MAX_NUM");
    Clear(0x50a338, 0xc);
    memory_.Store32(0x50a338, 0xffffffff);
    memory_.Store32(0x50a33c, count);
    Clear(0x50a2c0, 0x78);
    memory_.Store32(0x50a340, 0x50a2c0);
    for (int index = 0; index < count; ++index) {
        memory_.Store16(Port(index), static_cast<uint16_t>(index));
        memory_.Store16(Port(index) + 2, 0xffff);
        memory_.Store32(Port(index) + 0x1c, 4);
    }
}
int KtKeybdModel::NextIndex(int port, int index) {
    const auto next = static_cast<uint32_t>(index) + 1u;
    const auto capacity = std::bit_cast<int32_t>(memory_.Load32(Port(port) + 0x1c));
    return std::bit_cast<int32_t>(next) < capacity ? std::bit_cast<int32_t>(next) : 0;
}
void KtKeybdModel::AdvanceWrite(int port) {
    const auto address = Port(port) + 0x24;
    memory_.Store32(address, NextIndex(port, std::bit_cast<int32_t>(memory_.Load32(address))));
}
uint32_t KtKeybdModel::WriteEntry(int port) {
    const auto address = Port(port) + 0x28;
    if (!address) return 0;
    return address + memory_.Load32(Port(port) + 0x24) * 0x14u;
}
void KtKeybdModel::AdvanceRead(int port) {
    const auto address = Port(port) + 0x20;
    memory_.Store32(address, NextIndex(port, std::bit_cast<int32_t>(memory_.Load32(address))));
}
uint32_t KtKeybdModel::ReadEntry(int port) {
    const auto address = Port(port) + 0x28;
    if (!address) return 0;
    return address + memory_.Load32(Port(port) + 0x20) * 0x14u;
}
int KtKeybdModel::PortState(uint32_t address) {
    return std::bit_cast<int16_t>(memory_.Load16(address + 2));
}
int KtKeybdModel::State(int port) {
    AssertPort(port, 0x114);
    return PortState(Port(port));
}
int KtKeybdModel::PortType(uint32_t address) {
    if (PortState(address) == -1) return 0;
    return std::bit_cast<int32_t>(memory_.Load32(address + 4));
}
int KtKeybdModel::Status(int port) {
    AssertPort(port, 0x130);
    return PortType(Port(port));
}
void KtKeybdModel::RequestRepeat(int port, int value) {
    AssertPort(port, 0x183);
    memory_.Store32(Port(port) + 0x14, static_cast<uint32_t>(value) | 0x80000000u);
}
void KtKeybdModel::RequestLed(int port, int value) {
    AssertPort(port, 0x192);
    memory_.Store32(Port(port) + 0x18, static_cast<uint32_t>(value) | 0x80000000u);
}
int KtKeybdModel::RepeatRequest(int port) {
    AssertPort(port, 0x280);
    if (port < 0 || !IsAllocated(Port(port))) return 0;
    return std::bit_cast<int32_t>(memory_.Load32(Port(port) + 0x14));
}
void KtKeybdModel::ClearRepeatRequest(int port, int value) {
    AssertPort(port, 0x28f);
    if (port < 0 || !IsAllocated(Port(port))) return;
    memory_.Store32(Port(port) + 0x14, value);
}
int KtKeybdModel::LedRequest(int port) {
    AssertPort(port, 0x2a4);
    if (port < 0 || !IsAllocated(Port(port))) return 0;
    return std::bit_cast<int32_t>(memory_.Load32(Port(port) + 0x18));
}
void KtKeybdModel::ClearLedRequest(int port, int value) {
    AssertPort(port, 0x2b3);
    if (port < 0 || !IsAllocated(Port(port))) return;
    memory_.Store32(Port(port) + 0x18, value);
}
}
