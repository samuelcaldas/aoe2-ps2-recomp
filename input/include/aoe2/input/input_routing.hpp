#pragma once

namespace aoe2::input {

enum class OperationMode { Handheld, Docked };

// Which host input sources feed the guest in a given Switch operation mode.
struct InputSources {
    bool touchscreen{false};
    bool controller{false};
    bool usb_mouse{false};
    bool usb_keyboard{false};
};

// Handheld: touchscreen + attached Joy-Cons. Docked: controller + USB mouse and
// keyboard plugged into the dock, mirroring the PS2's USB peripherals.
InputSources SourcesFor(OperationMode mode);

} // namespace aoe2::input
