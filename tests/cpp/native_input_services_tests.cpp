#include <aoe2/game_hooks/native_input.hpp>
#include <aoe2/input/usb_keyboard_rpc.hpp>
#include <aoe2/input/usb_mouse_rpc.hpp>
#include "fake_pad_guest_memory.hpp"

#include <cstdlib>
#include <iostream>

namespace {

void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

void TestSharedMouseRpc() {
    FakePadGuestMemory memory;
    aoe2::game_hooks::BindMouseRpcMemory(reinterpret_cast<uint8_t*>(&memory));
    auto& rpc = aoe2::game_hooks::EnsureMouseRpc();
    Check(&rpc == &aoe2::game_hooks::EnsureMouseRpc(), "mouse rpc is singleton");
}

void TestSharedKeyboardRpc() {
    FakePadGuestMemory memory;
    aoe2::game_hooks::BindKeyboardRpcMemory(reinterpret_cast<uint8_t*>(&memory));
    auto& rpc = aoe2::game_hooks::EnsureKeyboardRpc();
    Check(&rpc == &aoe2::game_hooks::EnsureKeyboardRpc(), "keyboard rpc is singleton");
}

void TestInstallationFlags() {
    Check(!aoe2::game_hooks::NativeMouseModuleInstalled(), "mouse module starts uninstalled");
    Check(!aoe2::game_hooks::NativeKeyboardModuleInstalled(), "keyboard module starts uninstalled");
    aoe2::game_hooks::SetNativeMouseModuleInstalled(true);
    aoe2::game_hooks::SetNativeKeyboardModuleInstalled(true);
    Check(aoe2::game_hooks::NativeMouseModuleInstalled(), "mouse module marked installed");
    Check(aoe2::game_hooks::NativeKeyboardModuleInstalled(), "keyboard module marked installed");
    aoe2::game_hooks::SetNativeMouseModuleInstalled(false);
    aoe2::game_hooks::SetNativeKeyboardModuleInstalled(false);
    Check(!aoe2::game_hooks::NativeMouseModuleInstalled(), "mouse module cleared");
    Check(!aoe2::game_hooks::NativeKeyboardModuleInstalled(), "keyboard module cleared");
}

}  // namespace

int main() {
    TestSharedMouseRpc();
    TestSharedKeyboardRpc();
    TestInstallationFlags();
    std::cout << "native_input_services_tests: all passed\n";
    return 0;
}
