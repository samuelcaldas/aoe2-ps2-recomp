#pragma once

#include <aoe2/pad/pad_device.hpp>

#include <memory>

namespace aoe2::game_hooks {
// Host gamepad 0 through raylib; port 1 is always reported disconnected.
std::unique_ptr<aoe2::pad::PadBackend> MakeRaylibPadBackend();
}
