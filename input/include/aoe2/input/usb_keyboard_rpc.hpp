#pragma once

#include <aoe2/input/guest_keyboard.hpp>

#include <array>
#include <cstdint>
#include <deque>
#include <optional>

namespace aoe2::input {

class UsbKeyboardRpcModel {
public:
  static constexpr std::size_t kReplyBytes = 32;
  static constexpr std::size_t kPendingReports = 3;
  using Reply = std::array<uint8_t, kReplyBytes>;

  void SetConnected(bool connected);
  void SubmitReport(const GuestKeyboardState &report);
  Reply InfoReply() const;
  Reply ReadReply();
  Reply DeviceInfoReply() const;
  Reply SetLed(uint8_t unit, uint8_t value);
  Reply SetCapsType(uint8_t unit, uint8_t value);

private:
  uint8_t ValidateUnit(uint8_t unit) const;
  uint8_t LockMask(const GuestKeyboardState &report) const;

  bool connected_{false};
  uint8_t led_pattern_{0};
  uint8_t caps_type_{0};
  uint8_t previous_lock_mask_{0};
  std::deque<GuestKeyboardState> pending_reports_;
  std::optional<GuestKeyboardState> latest_report_;
};

} // namespace aoe2::input
