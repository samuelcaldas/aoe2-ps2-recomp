#include <aoe2/input/input_routing.hpp>

namespace aoe2::input {

InputSources SourcesFor(OperationMode mode) {
    if (mode == OperationMode::Docked) {
        return InputSources{.touchscreen = false, .controller = true, .usb_mouse = true, .usb_keyboard = true};
    }
    return InputSources{.touchscreen = true, .controller = true, .usb_mouse = false, .usb_keyboard = false};
}

} // namespace aoe2::input
