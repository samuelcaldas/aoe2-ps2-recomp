#pragma once

#include <aoe2/input/usb_keyboard_rpc.hpp>
#include <aoe2/input/usb_mouse_rpc.hpp>
#include <cstdint>
#include <mutex>

namespace aoe2::game_hooks::detail {

extern std::mutex g_mouse_mutex;
extern aoe2::input::UsbMouseRpcModel g_mouse_model;
// rdram may be null; absolute pointing needs it to read where the game's cursor is.
void SampleHostMouse(uint8_t* rdram);

extern std::mutex g_keyboard_mutex;
extern aoe2::input::UsbKeyboardRpcModel g_keyboard_model;
void SampleHostKeyboard();

}  // namespace aoe2::game_hooks::detail
