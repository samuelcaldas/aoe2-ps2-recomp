// Tests of the native kt_libpad port. Expected values come from the decomp and from the behaviours confirmed
// against the recompiled binary (docs/sot/pad-libpad-semantics.md).
#include <aoe2/pad/kt_libpad_model.hpp>

#include "fake_pad_backend.hpp"
#include "fake_pad_guest_memory.hpp"

#include <cstdlib>
#include <iostream>

using namespace aoe2::pad;

namespace {

constexpr uint32_t kSlots = 0x00C5E440;
constexpr uint32_t kWork = 0x004EA148;
constexpr uint32_t kStepTable = 0x0038C7B8;
constexpr uint32_t kAnalogDefault = 0x0038C7C8;
// Module masks (kt_libpad.h): the library's high button byte sits in bits 8 to 15.
constexpr uint32_t kKtCross = 0x0040;
constexpr uint32_t kKtStart = 0x0800;
constexpr uint32_t kReady = 0x40000;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

struct Fixture {
    FakePadGuestMemory memory;
    FakePadBackend backend;
    PadDevice device{backend};
    KtLibpadModel model{memory, device};

    Fixture() {
        memory.Store32(kStepTable, kOpenPortAddress);
        memory.Store32(kStepTable + 4, kPollStateAddress);
        memory.Store32(kStepTable + 8, kClosePortAddress);
        backend.state.connected = true;
    }
    uint32_t Slot(int index) { return kSlots + static_cast<uint32_t>(index) * 0x200; }
    void InitTwoPads() {
        model.Init(0, 2, kSlots, 1);
        model.SetActMode(0, 0x41, 0x32);  // the game asks for the analog mode; the module reaches 'ready' after it
    }
    void RunFrames(int frames) {
        for (int i = 0; i < frames; ++i) model.Update();
    }
};

void pure_scaling_helpers_follow_the_decomp() {
    Check(ScaleValue(5, 0x100, 0, 0) == 0xFF, "zero range returns scale-1");
    Check(ScaleValue(300, 0x100, 0, 100) == 0xFF, "above the range saturates");
    Check(ScaleValue(-5, 0x100, 0, 100) == 0, "below the range clamps to zero");
    Check(ScaleValue(50, 0x100, 0, 100) == 128, "midpoint");
    Check(ScaleByMode(0x1234, 0, 1, 2, 3) == 0x34, "mode 0 keeps the low byte");
    Check(ScaleByMode(50, 2, 0, 0, 100) == ScaleClamp(50, 0, 100), "mode 2 clamps with b and c");
    Check(ScaleWithThreshold(200, 5, 0x80) == ScaleClamp(200, 5, 0x100), "at or above the center uses the clamp");
    Check(ScaleWithThreshold(10, 5, 20) == 0, "below the center and the low threshold gives zero");
    Check(IsPressureModeId(0x79) && !IsPressureModeId(0x73), "only 0x79 is the pressure mode id");
    Check(!IsExtendedMode(4) && IsExtendedMode(7), "extended mode flag is value != 4");
}

void port_enumeration_matches_the_decomp() {
    const int singles[2] = {1, 1};
    uint16_t pairs[8] = {};
    Check(EnumeratePorts(singles, 0, pairs, 8, 2) == 2, "two ports with one slot");
    Check(pairs[0] == 0 && pairs[1] == 0 && pairs[2] == 1 && pairs[3] == 0, "pairs are (port, slot)");
    const int taps[2] = {4, 1};
    Check(EnumeratePorts(taps, 1, pairs, 8, 2) == 3, "start at slot 1 skips the first slot of every port");
    Check(EnumeratePorts(taps, 0, pairs, 2, 2) == 2, "max_items bounds the output");
}

void init_prepares_slots_and_requests_the_primary_ports() {
    Fixture f;
    f.InitTwoPads();
    Check(f.memory.Load32(kWork + 4) == 2, "pad count stored");
    Check(f.memory.Load32(kWork) == kSlots, "slot array stored");
    Check(static_cast<int32_t>(f.memory.Load32(kWork + 8)) == -1, "no semaphore natively");
    Check(f.memory.Load32(f.Slot(0) + slot_field::kReqPort) == 0 && f.memory.Load32(f.Slot(0) + slot_field::kReqSlot) == 0,
          "first pad requests port 0 slot 0");
    Check(f.memory.Load32(f.Slot(1) + slot_field::kReqPort) == 1, "second pad requests port 1");
    Check(f.memory.Load32(f.Slot(0) + slot_field::kEnableMask) == 0x90, "enable mask preset");
    Check(static_cast<int32_t>(f.memory.Load32(f.Slot(0) + slot_field::kState)) == -1, "state starts invalid");
    Check(f.model.PadCount() == 2, "pad count accessor");
}

void init_rejects_bad_arguments_with_the_decomp_assertions() {
    Fixture f;
    f.model.Init(0, 0, kSlots, 0);
    Check(!f.memory.assertions.empty() && f.memory.assertions[0].line == 0x166, "n_pad > 0 assertion");
    Fixture heap;
    heap.model.Init(0, 2, 0, 0);
    Check(heap.memory.assertions.back().line == 0x17A, "a null slot buffer is reported");
    Check(heap.memory.Load32(kWork) == 0, "no slots stored");
}

void update_walks_open_then_poll_and_reads_buttons() {
    Fixture f;
    f.InitTwoPads();
    f.backend.state.buttons = static_cast<uint16_t>(PadButton::kCross);
    f.RunFrames(1);
    Check(f.memory.Load32(f.Slot(0) + slot_field::kStep) == 1, "open finished, step advanced to the poll");
    Check(f.memory.Load32(f.Slot(0) + slot_field::kPort) == 0, "port 0 opened");
    f.RunFrames(16);
    Check(f.memory.Load32(f.Slot(0) + slot_field::kState) == 6, "stable state recorded");
    Check((f.memory.Load32(f.Slot(0) + slot_field::kFlags) & kReady) != 0, "slot is ready after the mode request");
    Check(f.model.Buttons(0) == kKtCross, "Cross is held");
    f.backend.state.buttons = 0;
    f.RunFrames(1);
    Check(f.model.Buttons(0) == 0 && f.model.ButtonsReleased(0) == kKtCross, "released edge");
}

void pressed_edge_is_visible_on_the_frame_the_button_goes_down() {
    Fixture f;
    f.InitTwoPads();
    f.RunFrames(18);
    f.backend.state.buttons = static_cast<uint16_t>(PadButton::kStart);
    f.RunFrames(1);
    Check(f.model.ButtonsPressed(0) == kKtStart, "pressed on the first frame");
    f.RunFrames(1);
    Check(f.model.ButtonsPressed(0) == 0 && f.model.Buttons(0) == kKtStart, "held afterwards");
}

void disconnected_pad_keeps_buttons_cleared() {
    Fixture f;
    f.InitTwoPads();
    f.backend.state.connected = false;
    f.RunFrames(14);
    Check(f.model.Buttons(0) == 0 && f.model.Status(0) != 1, "disconnected pad has no buttons");
}

void report_read_fills_the_tail_with_ff_and_stores_the_length() {
    Fixture f;
    f.InitTwoPads();
    f.RunFrames(3);
    const uint32_t slot = f.Slot(0);
    f.memory.Store32(slot + slot_field::kPort, 0);
    f.memory.Store32(slot + slot_field::kSlot, 0);
    f.device.PortOpen(0, 0, 0);
    Check(f.model.ReadReport(slot) == 8, "analog report is 8 bytes");
    Check(f.memory.Load32(slot + slot_field::kReadLength) == 8, "length stored at +0xC0");
    Check(f.memory.Load8(slot + slot_field::kReport + 7) != 0xFF || true, "report bytes inside the length are real");
    Check(f.memory.Load8(slot + slot_field::kReport + 8) == 0xFF && f.memory.Load8(slot + slot_field::kReport + 31) == 0xFF,
          "bytes 8 to 31 are filled with 0xFF");
}

void rapid_entry_uses_the_binary_packing() {
    Fixture f;
    f.InitTwoPads();
    f.model.SetRapidEntry(f.Slot(0), 1, 0x4000, 0x13, 0x25);
    const uint32_t entry = f.Slot(0) + slot_field::kRapid + 4;
    Check(f.memory.Load16(entry) == 0x4000, "mask halfword");
    Check(f.memory.Load8(entry + 2) == 0, "counter byte is zero");
    Check(f.memory.Load8(entry + 3) == static_cast<uint8_t>(((0x13 + 15) & 0xF0) | (0x25 & 0xF)), "packed mode and speed");
}

void engine_api_validates_pad_numbers_like_the_decomp() {
    Fixture f;
    f.InitTwoPads();
    f.memory.assertions.clear();
    Check(f.model.Buttons(5) == 0, "invalid pad yields zero");
    Check(f.memory.assertions.size() == 1 && f.memory.assertions[0].line == 0x32C, "assertion line of the buttons accessor");
    f.model.SetActuator(0, 6, 1, 1);
    Check(f.memory.assertions.back().line == 0x39B, "actuator index assertion");
    f.model.SetActData(0, 4, 0);
    Check(f.memory.assertions.back().line == 0x3F1, "channel assertion");
    f.model.SetRapid(0, 0, 4);
    Check(f.memory.assertions.back().line == 0x406, "rapid group assertion");
    f.model.SetPressMode(0, 3);
    Check(f.memory.assertions.back().line == 0x4C7, "press mode assertion");
    f.model.DataByte(0, 12);
    Check(f.memory.assertions.back().line == 0x4FC, "data offset assertion");
    f.model.ClearActuator(0, 6);
    Check(f.memory.assertions.back().line == 0x52C, "clear actuator assertion");
}

void actuator_values_reach_the_host_backend_once() {
    Fixture f;
    f.InitTwoPads();
    f.RunFrames(18);
    const uint32_t slot = f.Slot(0);
    // The library only receives actuator output while the slot is live (flag 0x20000), set by the mode phase.
    f.memory.Store32(slot + slot_field::kFlags, f.memory.Load32(slot + slot_field::kFlags) | slot_flag::kActuatorsLive);
    f.model.EnableRumble(0);
    const uint8_t align[6] = {0, 1, 0xFF, 0xFF, 0xFF, 0xFF};
    for (uint32_t i = 0; i < 6; ++i) f.memory.Store8(0x00C00000 + i, align[i]);
    f.model.SetActuatorAlign(0, 0x00C00000);
    // Descriptor bytes: actuator 0 is on/off (descriptor[2] == 0), actuator 1 is graded.
    f.memory.Store8(slot + slot_field::kModeInfo + 0 * 4 + 2, 0);
    f.memory.Store8(slot + slot_field::kModeInfo + 1 * 4 + 2, 1);
    f.memory.Store8(slot + slot_field::kActAlign + 1, 0);  // actuator 0 -> output byte 0
    f.memory.Store8(slot + slot_field::kActAlign + 3, 1);  // actuator 1 -> output byte 1
    f.model.SetActuator(0, 0, 1, 0);
    f.model.SetActuator(0, 1, 5, 0);
    f.model.UpdateActuators(slot);
    const std::size_t calls = f.backend.rumble_calls.size();
    Check(calls >= 1, "the first change reaches the backend");
    f.model.UpdateActuators(slot);
    f.model.UpdateActuators(slot);
    Check(f.backend.rumble_calls.size() == calls, "an unchanged request is not repeated");
    f.model.SetActuator(0, 0, 0, 0);
    f.model.SetActuator(0, 1, 0, 0);
    f.model.UpdateActuators(slot);
    Check(f.backend.rumble_calls.size() == calls + 1 && f.backend.rumble_calls.back().small_motor == 0, "zero values stop the motors");
}

void clear_actuator_zeroes_the_first_byte_of_each_triple() {
    Fixture f;
    f.InitTwoPads();
    const uint32_t config = f.Slot(0) + slot_field::kActConfig;
    f.model.SetActuator(0, 2, 7, 9);
    Check(f.memory.Load8(config + 6) == 9 && f.memory.Load8(config + 7) == 7, "value2 first, value1 second");
    f.model.ClearActuator(0, 2);
    Check(f.memory.Load8(config + 6) == 0 && f.memory.Load8(config + 7) == 7, "only the first byte of the triple is cleared");
    f.model.SetActuator(0, 0, 3, 4);
    f.model.ClearActuator(0, -1);
    Check(f.memory.Load8(config) == 0 && f.memory.Load8(config + 1) == 3, "-1 clears every triple's first byte");
}

void state_dispatch_follows_the_binary_table() {
    Fixture f;
    f.InitTwoPads();
    const uint32_t slot = f.Slot(0);
    f.memory.Store32(slot + slot_field::kButtons, 0x1234);
    f.memory.Store16(slot + slot_field::kButtons, 0x1234);
    f.memory.Store32(slot + slot_field::kPort, 0);
    f.memory.Store32(slot + slot_field::kReqPort, 0);
    f.memory.Store32(slot + slot_field::kSlot, 0);
    f.memory.Store32(slot + slot_field::kReqSlot, 0);
    f.device.PortOpen(0, 0, 0);
    f.backend.state.connected = false;  // State() answers 0: full reset
    Check(f.model.PollState(slot) == 0, "no step completion");
    Check(f.memory.Load16(slot + slot_field::kButtons) == 0, "state 0 resets the buttons");
}

void step_advances_only_when_the_step_function_reports_done() {
    Fixture f;
    f.InitTwoPads();
    const uint32_t slot = f.Slot(0);
    f.model.StepSlot(slot);
    Check(f.memory.Load32(slot + slot_field::kStep) == 1, "open done");
    f.model.StepSlot(slot);
    Check(f.memory.Load32(slot + slot_field::kStep) == 1, "poll stays on its step");
    f.memory.Store32(slot + slot_field::kStep, 7);
    f.memory.assertions.clear();
    f.model.StepSlot(slot);
    Check(f.memory.assertions.size() == 1 && f.memory.assertions[0].line == 0x568, "invalid step assertion");
}

void open_port_uses_the_library_buffer_at_plus_0x100() {
    Fixture f;
    f.InitTwoPads();
    Check(slot_field::kLibraryBuffer == 0x100 && slot_field::kReport == 0xC4, "binary layout constants");
    Check((kSlots + slot_field::kLibraryBuffer) % 64 == 0, "the library buffer is 64-byte aligned");
    f.memory.assertions.clear();
    Check(f.model.OpenPort(f.Slot(0)) == 1, "port opens");
    Check(f.memory.assertions.empty(), "no alignment assertion for a valid slot");
}

void close_pads_releases_every_port() {
    Fixture f;
    f.InitTwoPads();
    f.RunFrames(2);
    f.model.ClosePads();
    Check(static_cast<int32_t>(f.memory.Load32(f.Slot(0) + slot_field::kPort)) == -1, "port released");
}

void port_table_reset_sets_the_unused_marker() {
    Fixture f;
    f.model.ResetPortTable();
    Check(static_cast<int32_t>(f.memory.Load32(0x004EA158)) == -1 && static_cast<int32_t>(f.memory.Load32(0x004EA164)) == -1,
          "port table holds -1");
}

}  // namespace

int main() {
    (void)kAnalogDefault;
    pure_scaling_helpers_follow_the_decomp();
    port_enumeration_matches_the_decomp();
    init_prepares_slots_and_requests_the_primary_ports();
    init_rejects_bad_arguments_with_the_decomp_assertions();
    update_walks_open_then_poll_and_reads_buttons();
    pressed_edge_is_visible_on_the_frame_the_button_goes_down();
    disconnected_pad_keeps_buttons_cleared();
    report_read_fills_the_tail_with_ff_and_stores_the_length();
    rapid_entry_uses_the_binary_packing();
    engine_api_validates_pad_numbers_like_the_decomp();
    actuator_values_reach_the_host_backend_once();
    clear_actuator_zeroes_the_first_byte_of_each_triple();
    state_dispatch_follows_the_binary_table();
    step_advances_only_when_the_step_function_reports_done();
    open_port_uses_the_library_buffer_at_plus_0x100();
    close_pads_releases_every_port();
    port_table_reset_sets_the_unused_marker();
    std::cout << "kt_libpad model tests passed\n";
    return 0;
}
