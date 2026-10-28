#include "aoe2/platform/switch/switch_input.hpp"

namespace aoe2::platform::switch_env {

SwitchInputBridge::SwitchInputBridge(aoe2::input::InputSink* target_sink)
    : active_sink_(target_sink ? target_sink : &default_sink_) {
    handheld_translator_ = std::make_unique<aoe2::input::TouchGestureTranslator>(
        *active_sink_, handheld_mapping_);
    docked_translator_ = std::make_unique<aoe2::input::TouchGestureTranslator>(
        *active_sink_, docked_mapping_);
}

uint16_t SwitchInputBridge::MapButtonsToDualShock2(uint64_t switch_keys) {
    uint16_t ps2 = 0;

    // Direct DPAD mapping
    if (switch_keys & switch_btn::DUP)    ps2 |= ps2_pad::UP;
    if (switch_keys & switch_btn::DDOWN)  ps2 |= ps2_pad::DOWN;
    if (switch_keys & switch_btn::DLEFT)  ps2 |= ps2_pad::LEFT;
    if (switch_keys & switch_btn::DRIGHT) ps2 |= ps2_pad::RIGHT;

    // Face buttons:
    // Switch A -> PS2 Circle
    // Switch B -> PS2 Cross
    // Switch X -> PS2 Triangle
    // Switch Y -> PS2 Square
    if (switch_keys & switch_btn::A) ps2 |= ps2_pad::CIRCLE;
    if (switch_keys & switch_btn::B) ps2 |= ps2_pad::CROSS;
    if (switch_keys & switch_btn::X) ps2 |= ps2_pad::TRIANGLE;
    if (switch_keys & switch_btn::Y) ps2 |= ps2_pad::SQUARE;

    // Shoulder buttons
    if (switch_keys & switch_btn::L)  ps2 |= ps2_pad::L1;
    if (switch_keys & switch_btn::R)  ps2 |= ps2_pad::R1;
    if (switch_keys & switch_btn::ZL) ps2 |= ps2_pad::L2;
    if (switch_keys & switch_btn::ZR) ps2 |= ps2_pad::R2;

    // Sticks
    if (switch_keys & switch_btn::LSTICK) ps2 |= ps2_pad::L3;
    if (switch_keys & switch_btn::RSTICK) ps2 |= ps2_pad::R3;

    // System/Menu
    if (switch_keys & switch_btn::PLUS)  ps2 |= ps2_pad::START;
    if (switch_keys & switch_btn::MINUS) ps2 |= ps2_pad::SELECT;

    return ps2;
}

std::vector<aoe2::input::ActionEvent> SwitchInputBridge::ProcessTouch(
    const SwitchTouchPoint& touch,
    ConsoleOperationMode mode) {

    default_sink_.events.clear();
    aoe2::input::TouchContact contact{
        touch.id,
        touch.x,
        touch.y,
        touch.phase,
        touch.timestamp_us
    };

    if (mode == ConsoleOperationMode::Docked) {
        docked_translator_->Submit(contact);
    } else {
        handheld_translator_->Submit(contact);
    }

    return default_sink_.events;
}

aoe2::input::GuestPoint SwitchInputBridge::MapTouchToGuest(
    float host_x, float host_y,
    ConsoleOperationMode mode) const {
    if (mode == ConsoleOperationMode::Docked) {
        return docked_mapping_.ToGuest(host_x, host_y);
    }
    return handheld_mapping_.ToGuest(host_x, host_y);
}

} // namespace aoe2::platform::switch_env
