#include <aoe2/input/usb_mouse.hpp>

#include <algorithm>
#include <stdexcept>

namespace aoe2::input {

UsbMouseTranslator::UsbMouseTranslator(InputSink& sink, Extent guest, GuestPoint start, MouseConfig config)
    : sink_(sink), guest_(guest), config_(config), cursor_(start) {
    if (guest.width <= 0.0f || guest.height <= 0.0f) {
        throw std::invalid_argument("UsbMouseTranslator: guest extent must be positive");
    }
    if (config.guest_pixels_per_count <= 0.0f) {
        throw std::invalid_argument("UsbMouseTranslator: sensitivity must be positive");
    }
}

void UsbMouseTranslator::Submit(const HostMouseReport& report) {
    MoveBy(report);
    EmitButtonTransitions(report);
}

void UsbMouseTranslator::MoveBy(const HostMouseReport& report) {
    if (report.delta_x == 0.0f && report.delta_y == 0.0f) {
        return;
    }
    const float scale = config_.guest_pixels_per_count;
    cursor_.x = std::clamp(cursor_.x + report.delta_x * scale, 0.0f, guest_.width - 1.0f);
    cursor_.y = std::clamp(cursor_.y + report.delta_y * scale, 0.0f, guest_.height - 1.0f);
    Emit(ActionType::PointerMove, report.timestamp_us);
}

void UsbMouseTranslator::EmitButtonTransitions(const HostMouseReport& report) {
    const uint32_t pressed = report.buttons & ~buttons_;
    const uint32_t released = buttons_ & ~report.buttons;
    buttons_ = report.buttons;
    if (pressed & kMouseButtonLeft) {
        Emit(ActionType::PointerDown, report.timestamp_us);
    }
    if (released & kMouseButtonLeft) {
        Emit(ActionType::PointerUp, report.timestamp_us);
    }
    if (pressed & kMouseButtonRight) {
        Emit(ActionType::SecondaryClick, report.timestamp_us);
    }
}

void UsbMouseTranslator::Emit(ActionType type, uint64_t timestamp_us) {
    sink_.SubmitEvent(ActionEvent{.type = type, .x = cursor_.x, .y = cursor_.y, .timestamp_us = timestamp_us});
}

} // namespace aoe2::input
