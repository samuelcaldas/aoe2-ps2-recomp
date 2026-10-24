#pragma once

#include <aoe2/pad/pad_device.hpp>

class PS2Runtime;

namespace aoe2::game_hooks {
// The pad device shared by the Sony-library handlers (pad_device) and the native kt_libpad (pad).
// EnsureNativePadDevice installs the Sony handlers once (idempotent) and returns the device; the
// order of static registrations between the two installers is unspecified, so both call it.
aoe2::pad::PadDevice& EnsureNativePadDevice(PS2Runtime& runtime);
}  // namespace aoe2::game_hooks
