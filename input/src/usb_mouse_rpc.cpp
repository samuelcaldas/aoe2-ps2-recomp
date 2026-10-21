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
