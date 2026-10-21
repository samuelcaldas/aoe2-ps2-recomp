#include <aoe2/input/usb_keyboard_rpc.hpp>

#include <algorithm>

namespace aoe2::input {

void UsbKeyboardRpcModel::SetConnected(bool connected) {
    connected_ = connected;
    if (!connected_) {
        pending_reports_.clear();
        latest_report_.reset();
    }
}

void UsbKeyboardRpcModel::SubmitReport(const GuestKeyboardState& report) {
    if (!connected_ || (latest_report_ && *latest_report_ == report)) {
        return;
    }
    latest_report_ = report;
    if (pending_reports_.size() == kPendingReports) {
        pending_reports_.back() = report;
        return;
    }
    pending_reports_.push_back(report);
}

UsbKeyboardRpcModel::Reply UsbKeyboardRpcModel::InfoReply() const {
    Reply reply{};
    reply[0] = 1;
    reply[1] = connected_ ? 1 : 0;
    reply[2] = connected_ ? 1 : 0;
    return reply;
}

UsbKeyboardRpcModel::Reply UsbKeyboardRpcModel::ReadReply() {
    Reply reply{};
    if (!connected_ || pending_reports_.empty()) {
        reply[0] = led_pattern_;
        return reply;
    }
    const GuestKeyboardState report = pending_reports_.front();
    pending_reports_.pop_front();
    const uint8_t lock_mask = LockMask(report);
    reply[0] = static_cast<uint8_t>(previous_lock_mask_ ^ lock_mask);
    previous_lock_mask_ = lock_mask;
    reply[1] = 8;
    reply[2] = report.modifiers;
    std::copy(report.keys.begin(), report.keys.end(), reply.begin() + 4);
    return reply;
}

UsbKeyboardRpcModel::Reply UsbKeyboardRpcModel::DeviceInfoReply() const {
    // Device location is stored by the game but not interpreted by its consumer.
    return Reply{};
}

UsbKeyboardRpcModel::Reply UsbKeyboardRpcModel::SetLed(uint8_t unit, uint8_t value) {
    Reply reply{};
    reply[0] = ValidateUnit(unit);
    if (reply[0] != 0) {
        return reply;
    }
    led_pattern_ = value;
    return reply;
}

UsbKeyboardRpcModel::Reply UsbKeyboardRpcModel::SetCapsType(uint8_t unit, uint8_t value) {
    Reply reply{};
    reply[0] = ValidateUnit(unit);
    if (reply[0] == 0) {
        caps_type_ = value;
    }
    return reply;
}

uint8_t UsbKeyboardRpcModel::ValidateUnit(uint8_t unit) const {
    if (unit != 0) {
        return 0xFF;
    }
    return connected_ ? 0 : 0xFE;
}

uint8_t UsbKeyboardRpcModel::LockMask(const GuestKeyboardState&) const {
    return led_pattern_ & 0x07;
}

} // namespace aoe2::input
