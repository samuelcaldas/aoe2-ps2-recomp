#pragma once

#include <aoe2/input/actions.hpp>
#include <aoe2/input/surface_mapping.hpp>

#include <cstdint>

namespace aoe2::input {

// Button bits as reported by a USB HID mouse (and libnx HidMouseButton).
enum MouseButtonBit : uint32_t {
    kMouseButtonLeft = 1u << 0,
    kMouseButtonRight = 1u << 1,
    kMouseButtonMiddle = 1u << 2,
};

// One host USB mouse report: relative motion in mouse counts plus held buttons.
struct HostMouseReport {
    float delta_x{0.0f};
    float delta_y{0.0f};
    uint32_t buttons{0};
    uint64_t timestamp_us{0};
    float wheel{0.0f};
};

struct MouseConfig {
    float guest_pixels_per_count{1.0f};
};

// Turns a docked USB mouse into the same pointer actions the touchscreen
// produces, keeping an absolute cursor inside the guest framebuffer. The left
// button drags (PointerDown/Move/Up); the right button is a SecondaryClick.
// The wheel is not mapped: the original game has no camera zoom.
class UsbMouseTranslator {
public:
    // Throws std::invalid_argument if the guest extent or sensitivity is not positive.
    UsbMouseTranslator(InputSink& sink, Extent guest, GuestPoint start, MouseConfig config = {});

    void Submit(const HostMouseReport& report);

private:
    void MoveBy(const HostMouseReport& report);
    void EmitButtonTransitions(const HostMouseReport& report);
    void Emit(ActionType type, uint64_t timestamp_us);

    InputSink& sink_;
    Extent guest_;
    MouseConfig config_;
    GuestPoint cursor_;
    uint32_t buttons_{0};
};

} // namespace aoe2::input
