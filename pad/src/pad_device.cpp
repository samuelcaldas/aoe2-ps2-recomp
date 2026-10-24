#include <aoe2/pad/pad_device.hpp>

namespace aoe2::pad {
PadDevice::PadDevice(PadBackend& backend) : backend_(backend) {}
int PadDevice::Init(int) { return 1; }
int PadDevice::Start(int) { return 1; }
int PadDevice::SlotMax(int port) const { return port == 0 || port == 1 ? 1 : 0; }
int PadDevice::PortMax() const { return 2; }
bool PadDevice::Valid(int port, int slot) const { return port >= 0 && port < 2 && slot == 0; }
int PadDevice::PortOpen(int port, int slot, uint32_t) {
    if (!Valid(port, slot)) return 0;
    opened_[port] = true;
    rumble_changed_[port] = false;
    return 1;
}
int PadDevice::PortClose(int port, int slot) {
    if (!Valid(port, slot) || !opened_[port]) return 0;
    StopRumble(port);
    opened_[port] = false;
    return 1;
}
int PadDevice::State(int port, int slot) {
    if (!Valid(port, slot) || !opened_[port]) return 0;
    return backend_.Sample(port).connected ? 6 : 0;
}
int PadDevice::ReqState(int, int) const { return 0; }
int PadDevice::Read(int port, int slot, std::array<uint8_t, 32>& out) {
    if (!Valid(port, slot) || !opened_[port]) return 0;
    const HostPadState state = backend_.Sample(port);
    if (!state.connected) return 0;
    const PadReport report = BuildReport(state, modes_[port]);
    out = report.bytes;
    return report.length;
}
int PadDevice::InfoAct(int, int, int actuator, int term) const {
    if (actuator == -1 && term == 0) return 2;
    if ((actuator == 0 || actuator == 1) && term >= 1 && term <= 4)
        return actuator == 0 ? (term == 1 || term == 3 ? 1 : 0) : (term == 2 ? 1 : 0);
    return 0;
}
int PadDevice::InfoMode(int, int, int term, int offset) const {
    if (term == 4) {
        if (offset == -1) return 2;
        if (offset == 0) return 0x41;
        if (offset == 1) return 0x73;
        return 0;
    }
    if (term == 1) return 0x73;
    if (term == 2) return 0x79;
    if (term == 3) return 0;
    return 0;
}
int PadDevice::SetMainMode(int port, int slot, int offset, int) {
    if (!Valid(port, slot) || !opened_[port]) return 0;
    if (offset == 0x41) modes_[port] = PadMode::kDigital;
    else if (offset == 0x73) modes_[port] = PadMode::kAnalog;
    else if (offset == 0x79) modes_[port] = PadMode::kPressure;
    else return 0;
    return 1;
}
int PadDevice::InfoPressMode(int, int) const { return 1; }
int PadDevice::SetActAlign(int, int, const uint8_t[6]) { return 1; }
int PadDevice::SetActDirect(int port, int slot, const uint8_t data[6]) {
    if (!Valid(port, slot) || !opened_[port]) return 0;
    if (!backend_.Sample(port).connected) {
        StopRumble(port);
        return 1;
    }
    const uint16_t pair = static_cast<uint16_t>((data[0] ? 0x100 : 0) | data[1]);
    if (!rumble_changed_[port] || rumble_[port] != pair) {
        backend_.Rumble(port, data[0] ? 255 : 0, data[1]);
        rumble_[port] = pair;
        rumble_changed_[port] = true;
    }
    return 1;
}

void PadDevice::StopRumble(int port) {
    if (!rumble_changed_[port] || rumble_[port] == 0) return;
    backend_.Rumble(port, 0, 0);
    rumble_[port] = 0;
    rumble_changed_[port] = true;
}
}
