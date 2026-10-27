#pragma once

#include <aoe2/input/usb_mouse.hpp>

#include <array>
#include <cstdint>

namespace aoe2::input {

// Host-side model of the PS2 USB mouse RPC service the game polls through its
// KCET mouse layer: connection info, queued movement events, and device info,
// each answered in a 32-byte reply buffer. One host mouse appears on port 0.
// Motion is accumulated and split into signed 8-bit events, so large moves
// reach the game intact across several reads.
class UsbMouseRpcModel {
public:
    static constexpr std::size_t kReplyBytes = 32;
    using Reply = std::array<uint8_t, kReplyBytes>;

    void SetConnected(bool connected);
    void Submit(const HostMouseReport& report);
    // Absolute pointing: replaces the queued motion with the distance still to travel to the
    // target (target minus where the game's cursor is now), so repeated calls converge
    // instead of accumulating.
    void SteerTo(float remaining_x, float remaining_y, uint32_t buttons);

    // [0] port count, [1] connected count, [2] port 0 connected.
    Reply InfoReply() const;
    // [0] event length (4, or 0 when empty), [1] buttons, [2] dx, [3] dy, [4] wheel.
    Reply ReadReply();
    // Device information is not interpreted by the game beyond being stored.
    Reply DeviceInfoReply() const;

private:
    bool HasPendingEvent() const;

    bool connected_{false};
    uint32_t buttons_{0};
    uint32_t reported_buttons_{0};
    int pending_x_{0};
    int pending_y_{0};
    int pending_wheel_{0};
};

} // namespace aoe2::input
