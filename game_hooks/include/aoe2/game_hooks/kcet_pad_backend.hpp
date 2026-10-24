#pragma once

#include <aoe2/pad/pad_device.hpp>

#include <memory>

namespace aoe2::game_hooks {
// Host gamepad 0 through raylib; port 1 is always reported disconnected.
std::unique_ptr<aoe2::pad::PadBackend> MakeRaylibPadBackend();
// Diagnostics-only: port 0 is a connected pad whose Cross button toggles every 500 ms; rumble is logged.
// Selected with AOE2_PAD_SYNTHETIC=1 so headless runs (no gamepad) can exercise the data path.
std::unique_ptr<aoe2::pad::PadBackend> MakeSyntheticPadBackend();
}
