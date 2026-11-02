#pragma once

#include <aoe2/input/surface_mapping.hpp>

namespace aoe2::input {

// Ryujinx exposes absolute HID mouse coordinates in the Switch panel's
// 1280x720 coordinate space. Keep this separate from docked framebuffer size:
// the game's presentation can be 1920x1080 while host mouse coordinates remain
// in this panel coordinate space.
inline constexpr Extent kSwitchHidCoordinateExtent{1280.0f, 720.0f};

} // namespace aoe2::input
