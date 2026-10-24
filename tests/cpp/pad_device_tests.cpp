#include <aoe2/pad/pad_device.hpp>
#include "fake_pad_backend.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

void device_reports_open_connected_port_as_stable() {
    using namespace aoe2::pad;
    FakePadBackend backend;
    PadDevice device(backend);
    Check(device.Init(0) == 1 && device.Start(0) == 1, "initialization and start");
    Check(device.SlotMax(0) == 1 && device.SlotMax(1) == 1 && device.SlotMax(2) == 0, "slot max");
    Check(device.PortMax() == 2, "port max");
    Check(device.PortOpen(0, 0, 0) == 1, "open valid port");
    Check(device.State(0, 0) == 6, "opened connected port is stable");
    Check(device.PortClose(0, 0) == 1 && device.State(0, 0) == 0, "close port");
}

void read_uses_current_mode_and_connection() {
    using namespace aoe2::pad;
    FakePadBackend backend;
    PadDevice device(backend);
    std::array<uint8_t, 32> report{};
    device.PortOpen(0, 0, 0);
    for (const auto [mode, length] : {std::pair{0x41, 4}, {0x73, 8}, {0x79, 20}}) {
        device.SetMainMode(0, 0, mode, 0);
        Check(device.Read(0, 0, report) == length, "mode-dependent report length");
    }
    backend.state.connected = false;
    Check(device.State(0, 0) == 0 && device.Read(0, 0, report) == 0, "disconnected port");
}

void invalid_port_or_slot_fails() {
    using namespace aoe2::pad;
    FakePadBackend backend;
    PadDevice device(backend);
    Check(device.PortOpen(2, 0, 0) == 0 && device.PortOpen(0, 1, 0) == 0, "invalid open");
    std::array<uint8_t, 32> report{};
    Check(device.State(2, 0) == 0 && device.Read(0, 1, report) == 0, "invalid query");
}

void actuator_updates_are_deduplicated_and_zero_stops() {
    using namespace aoe2::pad;
    FakePadBackend backend;
    PadDevice device(backend);
    device.PortOpen(0, 0, 0);
    const std::array<uint8_t, 6> active{1, 200};
    const std::array<uint8_t, 6> stopped{0, 0};
    device.SetActDirect(0, 0, active.data());
    device.SetActDirect(0, 0, active.data());
    device.SetActDirect(0, 0, stopped.data());
    Check(backend.rumble_calls.size() == 2, "duplicate rumble suppressed");
    Check(backend.rumble_calls.back().small_motor == 0 && backend.rumble_calls.back().large_motor == 0, "zero stops motors");
}

void disconnect_stops_motor_and_allows_same_command_after_reconnect() {
    using namespace aoe2::pad;
    FakePadBackend backend;
    PadDevice device(backend);
    device.PortOpen(0, 0, 0);
    const std::array<uint8_t, 6> active{1, 200};
    const std::array<uint8_t, 6> stopped{0, 0};
    device.SetActDirect(0, 0, active.data());
    backend.state.connected = false;
    device.SetActDirect(0, 0, stopped.data());
    Check(backend.rumble_calls.size() == 2, "disconnect stops active motor");
    Check(backend.rumble_calls.back().small_motor == 0 && backend.rumble_calls.back().large_motor == 0, "disconnect sends stop pair");
    backend.state.connected = true;
    device.SetActDirect(0, 0, active.data());
    Check(backend.rumble_calls.size() == 3, "reconnect forwards same command after stop");
}
}

int main() {
    device_reports_open_connected_port_as_stable();
    read_uses_current_mode_and_connection();
    invalid_port_or_slot_fails();
    actuator_updates_are_deduplicated_and_zero_stops();
    disconnect_stops_motor_and_allows_same_command_after_reconnect();
}
