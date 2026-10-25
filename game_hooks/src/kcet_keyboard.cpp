// KCET host input replacements; guest addresses are loaded from local configuration.
#include <aoe2/game_hooks/kcet_handlers.hpp>
#include <aoe2/input/guest_keyboard.hpp>
#include <aoe2/input/usb_keyboard_rpc.hpp>
#include "input_synthetic.hpp"
#include "native_input_internal.hpp"
#include <bitset>
#include <utility>
#include "raylib.h"
#include "runtime/ps2_memory.h"

#include <cstring>
#include <mutex>

namespace aoe2::game_hooks::detail {
std::mutex g_keyboard_mutex;
aoe2::input::UsbKeyboardRpcModel g_keyboard_model;
}  // namespace aoe2::game_hooks::detail

namespace {
constexpr int kRegA0 = 4;
constexpr int kRegA1 = 5;
constexpr int kRegRa = 31;
using aoe2::input::GuestKeyboardAdapter;
using aoe2::input::UsbKeyboardRpcModel;
using aoe2::game_hooks::detail::g_keyboard_mutex;
using aoe2::game_hooks::detail::g_keyboard_model;
GuestKeyboardAdapter g_keyboard_adapter;
std::bitset<256> g_previous_host_keys;

// raylib key -> USB HID keyboard usage (HID Usage Tables, keyboard page 0x07).
constexpr std::pair<int, uint8_t> kHostKeyboardUsages[] = {
    {KEY_A, 0x04}, {KEY_B, 0x05}, {KEY_C, 0x06}, {KEY_D, 0x07}, {KEY_E, 0x08}, {KEY_F, 0x09},
    {KEY_G, 0x0A}, {KEY_H, 0x0B}, {KEY_I, 0x0C}, {KEY_J, 0x0D}, {KEY_K, 0x0E}, {KEY_L, 0x0F},
    {KEY_M, 0x10}, {KEY_N, 0x11}, {KEY_O, 0x12}, {KEY_P, 0x13}, {KEY_Q, 0x14}, {KEY_R, 0x15},
    {KEY_S, 0x16}, {KEY_T, 0x17}, {KEY_U, 0x18}, {KEY_V, 0x19}, {KEY_W, 0x1A}, {KEY_X, 0x1B},
    {KEY_Y, 0x1C}, {KEY_Z, 0x1D},
    {KEY_ONE, 0x1E}, {KEY_TWO, 0x1F}, {KEY_THREE, 0x20}, {KEY_FOUR, 0x21}, {KEY_FIVE, 0x22},
    {KEY_SIX, 0x23}, {KEY_SEVEN, 0x24}, {KEY_EIGHT, 0x25}, {KEY_NINE, 0x26}, {KEY_ZERO, 0x27},
    {KEY_ENTER, 0x28}, {KEY_ESCAPE, 0x29}, {KEY_BACKSPACE, 0x2A}, {KEY_TAB, 0x2B}, {KEY_SPACE, 0x2C},
    {KEY_MINUS, 0x2D}, {KEY_EQUAL, 0x2E}, {KEY_LEFT_BRACKET, 0x2F}, {KEY_RIGHT_BRACKET, 0x30},
    {KEY_BACKSLASH, 0x31}, {KEY_SEMICOLON, 0x33}, {KEY_APOSTROPHE, 0x34}, {KEY_GRAVE, 0x35},
    {KEY_COMMA, 0x36}, {KEY_PERIOD, 0x37}, {KEY_SLASH, 0x38}, {KEY_CAPS_LOCK, 0x39},
    {KEY_F1, 0x3A}, {KEY_F2, 0x3B}, {KEY_F3, 0x3C}, {KEY_F4, 0x3D}, {KEY_F5, 0x3E}, {KEY_F6, 0x3F},
    {KEY_F7, 0x40}, {KEY_F8, 0x41}, {KEY_F9, 0x42}, {KEY_F10, 0x43}, {KEY_F11, 0x44}, {KEY_F12, 0x45},
    {KEY_PRINT_SCREEN, 0x46}, {KEY_SCROLL_LOCK, 0x47}, {KEY_PAUSE, 0x48}, {KEY_INSERT, 0x49},
    {KEY_HOME, 0x4A}, {KEY_PAGE_UP, 0x4B}, {KEY_DELETE, 0x4C}, {KEY_END, 0x4D}, {KEY_PAGE_DOWN, 0x4E},
    {KEY_RIGHT, 0x4F}, {KEY_LEFT, 0x50}, {KEY_DOWN, 0x51}, {KEY_UP, 0x52},
    {KEY_LEFT_CONTROL, 0xE0}, {KEY_LEFT_SHIFT, 0xE1}, {KEY_LEFT_ALT, 0xE2}, {KEY_LEFT_SUPER, 0xE3},
    {KEY_RIGHT_CONTROL, 0xE4}, {KEY_RIGHT_SHIFT, 0xE5}, {KEY_RIGHT_ALT, 0xE6}, {KEY_RIGHT_SUPER, 0xE7},
};

void ReturnToCaller(R5900Context* ctx, uint32_t value) {
    setReturnU32(ctx, value);
    ctx->pc = getRegU32(ctx, kRegRa);
}
}  // namespace

namespace aoe2::game_hooks::detail {
void SampleHostKeyboard() {
    g_keyboard_model.SetConnected(true);
    std::bitset<256> current;
    if (aoe2::game_hooks::synthetic::Enabled()) {
        using aoe2::game_hooks::synthetic::LogicalSteps;
        using aoe2::game_hooks::synthetic::Module;
        current = aoe2::game_hooks::synthetic::KeyboardState(LogicalSteps(Module::kKeyboard).load());
    } else {
        for (const auto& [host_key, usage] : kHostKeyboardUsages) {
            current.set(usage, IsKeyDown(static_cast<KeyboardKey>(host_key)));
        }
    }
    for (const auto& event : aoe2::input::DiffPressedKeys(g_previous_host_keys, current)) {
        g_keyboard_adapter.Submit(event);
        g_keyboard_model.SubmitReport(g_keyboard_adapter.NextFrame());
    }
    g_previous_host_keys = current;
}
}  // namespace aoe2::game_hooks::detail

namespace {
using aoe2::game_hooks::detail::SampleHostKeyboard;

void WriteKeyboardReply(uint8_t* rdram, uint32_t guest_address,
                        const UsbKeyboardRpcModel::Reply& reply) {
    uint8_t* destination = getMemPtr(rdram, guest_address);
    if (destination != nullptr) {
        std::memcpy(destination, reply.data(), reply.size());
    }
}

void KeyboardBind(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    ReturnToCaller(ctx, 0);
}

void KeyboardGetInfo(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_keyboard_mutex);
    SampleHostKeyboard();
    WriteKeyboardReply(rdram, getRegU32(ctx, kRegA0), g_keyboard_model.InfoReply());
    ReturnToCaller(ctx, 0);
}

void KeyboardRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_keyboard_mutex);
    const bool first_port = getRegU32(ctx, kRegA0) == 0;
    const auto reply = first_port ? g_keyboard_model.ReadReply() : UsbKeyboardRpcModel::Reply{};
    WriteKeyboardReply(rdram, getRegU32(ctx, kRegA1), reply);
    ReturnToCaller(ctx, 0);
}

void KeyboardDeviceInfo(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_keyboard_mutex);
    WriteKeyboardReply(rdram, getRegU32(ctx, kRegA1), g_keyboard_model.DeviceInfoReply());
    ReturnToCaller(ctx, 0);
}

void KeyboardSetLed(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_keyboard_mutex);
    const auto reply = g_keyboard_model.SetLed(
        static_cast<uint8_t>(getRegU32(ctx, kRegA0)),
        static_cast<uint8_t>(getRegU32(ctx, kRegA1)));
    ReturnToCaller(ctx, reply[0]);
}

void KeyboardSetCapsType(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_keyboard_mutex);
    const auto reply = g_keyboard_model.SetCapsType(
        static_cast<uint8_t>(getRegU32(ctx, kRegA0)),
        static_cast<uint8_t>(getRegU32(ctx, kRegA1)));
    ReturnToCaller(ctx, reply[0]);
}

} // namespace

namespace aoe2::game_hooks {
void RegisterKeyboardHandlers(KcetHandlers& handlers) {
    handlers.emplace("kcet.keyboard.bind", KeyboardBind);
    handlers.emplace("kcet.keyboard.get_info", KeyboardGetInfo);
    handlers.emplace("kcet.keyboard.read", KeyboardRead);
    handlers.emplace("kcet.keyboard.device_info", KeyboardDeviceInfo);
    handlers.emplace("kcet.keyboard.set_led", KeyboardSetLed);
    handlers.emplace("kcet.keyboard.set_caps_type", KeyboardSetCapsType);
}
}
