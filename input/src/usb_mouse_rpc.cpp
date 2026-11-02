#include <aoe2/input/usb_mouse_rpc.hpp>

#include <algorithm>
#include <cmath>

namespace aoe2::input {

namespace {

constexpr int kEventMaxDelta = 127;
constexpr uint8_t kEventLength = 4;
constexpr uint32_t kReportedButtons = kMouseButtonLeft | kMouseButtonRight | kMouseButtonMiddle;

int TakeStep(int& pending) {
    const int step = std::clamp(pending, -kEventMaxDelta, kEventMaxDelta);
    pending -= step;
    return step;
}

} // namespace

void UsbMouseRpcModel::SetConnected(bool connected) {
    connected_ = connected;
}

void UsbMouseRpcModel::Submit(const HostMouseReport& report) {
    pending_x_ += static_cast<int>(std::lround(report.delta_x));
    pending_y_ += static_cast<int>(std::lround(report.delta_y));
    pending_wheel_ += static_cast<int>(std::lround(report.wheel));
    buttons_ = report.buttons & kReportedButtons;
}

void UsbMouseRpcModel::SubmitMotion(float delta_x, float delta_y) {
    const float total_x = motion_remainder_x_ + delta_x;
    const float total_y = motion_remainder_y_ + delta_y;
    const int whole_x = static_cast<int>(std::lround(total_x));
    const int whole_y = static_cast<int>(std::lround(total_y));
    pending_x_ += whole_x;
    pending_y_ += whole_y;
    motion_remainder_x_ = total_x - whole_x;
    motion_remainder_y_ = total_y - whole_y;
}

void UsbMouseRpcModel::SteerTo(float remaining_x, float remaining_y, uint32_t buttons) {
    pending_x_ = static_cast<int>(std::lround(remaining_x));
    pending_y_ = static_cast<int>(std::lround(remaining_y));
    buttons_ = buttons & kReportedButtons;
    motion_remainder_x_ = 0.0f;
    motion_remainder_y_ = 0.0f;
}

UsbMouseRpcModel::Reply UsbMouseRpcModel::InfoReply() const {
    Reply reply{};
    reply[0] = 1;
    reply[1] = connected_ ? 1 : 0;
    reply[2] = connected_ ? 1 : 0;
    return reply;
}

UsbMouseRpcModel::Reply UsbMouseRpcModel::ReadReply() {
    Reply reply{};
    if (!connected_ || !HasPendingEvent()) {
        return reply;
    }
    reply[0] = kEventLength;
    reply[1] = static_cast<uint8_t>(buttons_);
    reply[2] = static_cast<uint8_t>(static_cast<int8_t>(TakeStep(pending_x_)));
    reply[3] = static_cast<uint8_t>(static_cast<int8_t>(TakeStep(pending_y_)));
    reply[4] = static_cast<uint8_t>(static_cast<int8_t>(TakeStep(pending_wheel_)));
    reported_buttons_ = buttons_;
    return reply;
}

UsbMouseRpcModel::Reply UsbMouseRpcModel::DeviceInfoReply() const {
    return Reply{};
}

bool UsbMouseRpcModel::HasPendingEvent() const {
    return pending_x_ != 0 || pending_y_ != 0 || pending_wheel_ != 0 || buttons_ != reported_buttons_;
}

} // namespace aoe2::input
