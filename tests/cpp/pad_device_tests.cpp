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
    Check(device.State(0, 0) == 1 && device.State(0, 0) == 1, "a fresh port is first found as a pad");
    for (int poll = 0; poll < 8; ++poll) Check(device.State(0, 0) == 2, "then the controller type is checked");
    Check(device.State(0, 0) == 6 && device.State(0, 0) == 6, "opened connected port becomes stable");
    backend.state.connected = false;
    Check(device.State(0, 0) == 0, "an unplugged pad reads as disconnected");
    backend.state.connected = true;
    Check(device.State(0, 0) == 1, "plugging it back restarts the sequence");
    Check(device.PortClose(0, 0) == 1, "close port");
    Check(device.State(0, 0) == 0, "closed port is disconnected from libpad state");
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

void set_main_mode_takes_a_mode_table_index_or_an_id() {
    aoe2::pad::FakePadBackend backend;
    aoe2::pad::PadDevice device(backend);
    std::array<uint8_t, 32> report{};
    Check(device.PortOpen(0, 0, 0) == 1, "open");
    Check(device.SetMainMode(0, 0, 0, 0) == 1 && device.Read(0, 0, report) == 4, "index 0 is digital");
    Check(device.SetMainMode(0, 0, 1, 0) == 1 && device.Read(0, 0, report) == 8, "index 1 is analog");
    Check(device.SetMainMode(0, 0, 0x79, 0) == 1 && device.Read(0, 0, report) == 20, "id 0x79 is pressure");
    Check(device.SetMainMode(0, 0, 7, 0) == 0, "unknown mode fails");
    Check(device.InfoMode(0, 0, 4, 0) == 0x41 && device.InfoMode(0, 0, 4, 1) == 0x73, "table entries are ids");
}

void disconnect_clears_the_caller_buffer_and_align_validates_the_port() {
    aoe2::pad::FakePadBackend backend;
    aoe2::pad::PadDevice device(backend);
    std::array<uint8_t, 32> report{};
    const uint8_t align[6] = {0, 1, 0xFF, 0xFF, 0xFF, 0xFF};
    backend.state.connected = true;
    backend.state.buttons = 0x4000;
    Check(device.SetActAlign(0, 0, align) == 0, "align on a closed port fails");
    Check(device.PortOpen(0, 0, 0) == 1 && device.SetActAlign(0, 0, align) == 1, "align on an open port");
    Check(device.SetActAlign(2, 0, align) == 0 && device.SetActAlign(0, 1, align) == 0, "align on invalid port or slot fails");
    Check(device.Read(0, 0, report) > 0 && report[3] != 0xFF, "connected read fills the report");
    backend.state.connected = false;
    Check(device.Read(0, 0, report) == 0, "disconnected read returns zero");
    Check(report == std::array<uint8_t, 32>{}, "disconnected read leaves no stale bytes");
}

int main() {
    set_main_mode_takes_a_mode_table_index_or_an_id();
    disconnect_clears_the_caller_buffer_and_align_validates_the_port();
    device_reports_open_connected_port_as_stable();
    read_uses_current_mode_and_connection();
    invalid_port_or_slot_fails();
    actuator_updates_are_deduplicated_and_zero_stops();
    disconnect_stops_motor_and_allows_same_command_after_reconnect();
}
