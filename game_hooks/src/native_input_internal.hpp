#pragma once

#include <aoe2/input/usb_keyboard_rpc.hpp>
#include <aoe2/input/usb_mouse_rpc.hpp>
#include <mutex>

namespace aoe2::game_hooks::detail {

extern std::mutex g_mouse_mutex;
extern aoe2::input::UsbMouseRpcModel g_mouse_model;
void SampleHostMouse();

extern std::mutex g_keyboard_mutex;
extern aoe2::input::UsbKeyboardRpcModel g_keyboard_model;
void SampleHostKeyboard();

}  // namespace aoe2::game_hooks::detail
