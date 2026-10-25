// Shared RPC services for native input modules and host device handlers.
#pragma once

#include <aoe2/keyboard/kt_keybd_model.hpp>
#include <aoe2/mouse/kt_mouse_model.hpp>

#include <cstdint>

namespace aoe2::game_hooks {

// Thread-safe service singletons shared between the device hooks (kcet_hooks.ini)
// and the native module handlers (native_modules.ini).
aoe2::mouse::MouseRpc& EnsureMouseRpc();
aoe2::keyboard::KeyboardRpc& EnsureKeyboardRpc();

// Binds the active RDRAM buffer for the current guest thread context.
void BindMouseRpcMemory(uint8_t* rdram);
void BindKeyboardRpcMemory(uint8_t* rdram);

// Returns true when the respective native module override is installed.
// When installed, the module handler performs snapshot capture at return,
// so call-trace hooks must avoid logging duplicate snapshots.
bool NativeMouseModuleInstalled();
bool NativeKeyboardModuleInstalled();
void SetNativeMouseModuleInstalled(bool installed);
void SetNativeKeyboardModuleInstalled(bool installed);

}  // namespace aoe2::game_hooks
