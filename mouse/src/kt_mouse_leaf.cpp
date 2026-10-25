// Origin: samuelcaldas/age-of-empires-2-decomp, kt_mouse.c,
// commit 945f15c56d02bed9de523870850f53784e5ad609. Binary-checked native guest-memory port.
#include <aoe2/mouse/kt_mouse_model.hpp>
#include <bit>

namespace aoe2::mouse {
KtMouseModel::KtMouseModel(runtime::GuestAccess& memory, MouseRpc& service)
    : memory_(memory), service_(service) {}

// These are real empty returns in the owned ELF, not hardware-service substitutes.
void KtMouseModel::WorkPadding() {}
void KtMouseModel::DevicePadding() {}

void KtMouseModel::Clear(uint32_t address, uint32_t size) {
    for (uint32_t index = 0; index < size; ++index) memory_.Store8(address + index, 0);
}
uint32_t KtMouseModel::Port(int port) {
    return memory_.Load32(0x50a068) + static_cast<uint32_t>(port) * 0xb0u;
}
void KtMouseModel::AssertPort(int port, int line) {
    if (port < std::bit_cast<int32_t>(memory_.Load32(0x50a064))) return;
    const auto expression = line < 0x23b ? "mouse_number < W->n_mouse" : "number < W->n_mouse";
    memory_.ReportAssertion("kt_mouse.c", line, expression);
}
void KtMouseModel::Init(int count) {
    if (count > 2) memory_.ReportAssertion("kt_mouse.c", 100, "n_mouse <= KT_MOUSE_MAX_NUM");
    Clear(0x50a060, 0x10);
    memory_.Store32(0x50a060, 0xffffffff);
    memory_.Store32(0x50a064, static_cast<uint32_t>(count));
    Clear(0x509f00, 0x160);
    memory_.Store32(0x50a068, 0x509f00);
    for (int index = 0; index < count; ++index) {
        memory_.Store16(Port(index), static_cast<uint16_t>(index));
        memory_.Store16(Port(index) + 2, 0xffff);
        memory_.Store32(Port(index) + 0x14, 4);
    }
}
int KtMouseModel::NextIndex(int port, int index) {
    const auto next = static_cast<uint32_t>(index) + 1u;
    const auto capacity = std::bit_cast<int32_t>(memory_.Load32(Port(port) + 0x14));
    return std::bit_cast<int32_t>(next) < capacity ? std::bit_cast<int32_t>(next) : 0;
}
void KtMouseModel::AdvanceWrite(int port) {
    const auto address = Port(port) + 0x1c;
    memory_.Store32(address, NextIndex(port, std::bit_cast<int32_t>(memory_.Load32(address))));
}
uint32_t KtMouseModel::WriteEntry(int port) {
    const auto address = Port(port) + 0x20;
    if (!address) return 0;
    return address + memory_.Load32(Port(port) + 0x1c) * 0x24u;
}
void KtMouseModel::AdvanceRead(int port) {
    const auto address = Port(port) + 0x18;
    memory_.Store32(address, NextIndex(port, std::bit_cast<int32_t>(memory_.Load32(address))));
}
uint32_t KtMouseModel::ReadEntry(int port) {
    const auto address = Port(port) + 0x20;
    if (!address) return 0;
    return address + memory_.Load32(Port(port) + 0x18) * 0x24u;
}
int KtMouseModel::PortState(uint32_t address) {
    return std::bit_cast<int16_t>(memory_.Load16(address + 2));
}
int KtMouseModel::State(int port) {
    AssertPort(port, 0x16b);
    return PortState(Port(port));
}
int KtMouseModel::PortType(uint32_t address) {
    if (PortState(address) == -1) return 0;
    return std::bit_cast<int32_t>(memory_.Load32(address + 4));
}
int KtMouseModel::Status(int port) {
    AssertPort(port, 0x188);
    return PortType(Port(port));
}
void KtMouseModel::Enable() {
    memory_.Store32(0x50a06c, 1);
}
void KtMouseModel::Disable() {
    memory_.Store32(0x50a06c, 0);
}
bool KtMouseModel::IsAllocated(uint32_t address) {
    return PortState(address) != -1;
}
int KtMouseModel::Enabled() {
    return std::bit_cast<int32_t>(memory_.Load32(0x50a06c));
}
}
