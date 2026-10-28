// KCET host input replacements; guest addresses are loaded from local configuration.
#include <aoe2/game_hooks/kcet_handlers.hpp>
#include <aoe2/input/presentation_transform.hpp>
#include <aoe2/input/usb_mouse_rpc.hpp>
#include "input_synthetic.hpp"
#include "native_input_internal.hpp"
#include "raylib.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>

namespace aoe2::game_hooks::detail {
std::mutex g_mouse_mutex;
aoe2::input::UsbMouseRpcModel g_mouse_model;
}  // namespace aoe2::game_hooks::detail

namespace {
constexpr int kRegA0 = 4;
constexpr int kRegA1 = 5;
constexpr int kRegRa = 31;
using aoe2::input::HostMouseReport;
using aoe2::input::UsbMouseRpcModel;
using aoe2::game_hooks::detail::g_mouse_mutex;
using aoe2::game_hooks::detail::g_mouse_model;
Vector2 g_last_position{};
aoe2::game_hooks::synthetic::SyntheticMouse g_synthetic_mouse;
bool g_has_last_position = false;
void ReturnToCaller(R5900Context* ctx, uint32_t value) {
    setReturnU32(ctx, value);
    ctx->pc = getRegU32(ctx, kRegRa);
}

uint32_t HostButtons() {
    uint32_t buttons = 0;
    buttons |= IsMouseButtonDown(MOUSE_BUTTON_LEFT) ? aoe2::input::kMouseButtonLeft : 0u;
    buttons |= IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? aoe2::input::kMouseButtonRight : 0u;
    buttons |= IsMouseButtonDown(MOUSE_BUTTON_MIDDLE) ? aoe2::input::kMouseButtonMiddle : 0u;
    return buttons;
}

// Deltas come from absolute positions so repeated polls within one host frame
// never count the same motion twice.
void SampleSyntheticMouse() {
    using aoe2::game_hooks::synthetic::LogicalSteps;
    using aoe2::game_hooks::synthetic::Module;
    g_mouse_model.SetConnected(true);
    g_mouse_model.Submit(g_synthetic_mouse.Next(LogicalSteps(Module::kMouse).load()));
}

// The game's cursor, in its 640x448 screen, as stored by its set-cursor-position routine
// (0x2257C0, which clamps x to 0..628 and y to 0..431).
constexpr uint32_t kGuestCursorX = 0x00C5E840;
constexpr uint32_t kGuestCursorY = 0x00C5E844;
constexpr float kGuestScreenWidth = 640.0f;
constexpr float kGuestScreenHeight = 448.0f;
// Set once hooks are installed; the runtime publishes the GS frame size it presents.
std::atomic<const PS2Runtime*> g_presentation_runtime{nullptr};

int32_t LoadGuestInt(uint8_t* rdram, uint32_t address) {
    int32_t value = 0;
    if (const uint8_t* source = getMemPtr(rdram, address)) std::memcpy(&value, source, sizeof(value));
    return value;
}

void LogButtonChange(uint32_t buttons) {
    static uint32_t last_buttons = 0;
    if (buttons != last_buttons) std::cerr << "[host-mouse] buttons=0x" << std::hex << buttons << std::dec << std::endl;
    last_buttons = buttons;
}

// AOE2_LOG_POINTER=1: one line per change of window position or guest cursor (calibration evidence).
void LogPointer(Vector2 window, aoe2::input::GuestPoint target, int32_t cursor_x, int32_t cursor_y) {
    static const bool enabled = std::getenv("AOE2_LOG_POINTER") != nullptr;
    static int32_t last[4] = {-1, -1, -1, -1};
    const int32_t now[4] = {static_cast<int32_t>(window.x), static_cast<int32_t>(window.y), cursor_x, cursor_y};
    if (!enabled || std::equal(now, now + 4, last)) return;
    std::copy(now, now + 4, last);
    std::cerr << "[host-pointer] window=" << now[0] << "," << now[1] << " target=" << target.x << "," << target.y
              << " cursor=" << cursor_x << "," << cursor_y << std::endl;
}

// The game's 640x448 screen fills whatever frame the runtime draws (640x512 once PAL display is set).
aoe2::input::PresentationTransform CurrentPresentation() {
    uint32_t width = 0;
    uint32_t height = 0;
    if (const PS2Runtime* runtime = g_presentation_runtime.load(std::memory_order_acquire)) runtime->presentedFrameExtent(width, height);
    const aoe2::input::Extent frame = width != 0 && height != 0
        ? aoe2::input::Extent{static_cast<float>(width), static_cast<float>(height)}
        : aoe2::input::Extent{kGuestScreenWidth, kGuestScreenHeight};
    return aoe2::input::PresentationTransform::LogicalFillsFrame(
        {static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, frame,
        {kGuestScreenWidth, kGuestScreenHeight});
}

// Absolute pointing: the host pointer's place in the window is where the game's cursor should be.
void SteerGuestCursor(uint8_t* rdram, Vector2 position, uint32_t buttons) {
    const auto target = CurrentPresentation().ToLogical(position.x, position.y);
    const int32_t cursor_x = LoadGuestInt(rdram, kGuestCursorX);
    const int32_t cursor_y = LoadGuestInt(rdram, kGuestCursorY);
    LogPointer(position, target, cursor_x, cursor_y);
    g_mouse_model.SteerTo(target.x - static_cast<float>(cursor_x), target.y - static_cast<float>(cursor_y), buttons);
}
}  // namespace

namespace aoe2::game_hooks::detail {
void SampleHostMouse(uint8_t* rdram) {
    if (aoe2::game_hooks::synthetic::Enabled()) return SampleSyntheticMouse();
    const Vector2 position = GetMousePosition();
    const uint32_t buttons = HostButtons();
    g_mouse_model.SetConnected(true);
    LogButtonChange(buttons);
    if (rdram != nullptr && IsCursorOnScreen()) return SteerGuestCursor(rdram, position, buttons);
    const Vector2 last = g_has_last_position ? g_last_position : position;
    g_last_position = position;
    g_has_last_position = true;
    g_mouse_model.Submit(HostMouseReport{position.x - last.x, position.y - last.y, buttons, 0, GetMouseWheelMove()});
}
}  // namespace aoe2::game_hooks::detail

namespace {
using aoe2::game_hooks::detail::SampleHostMouse;

void WriteReply(uint8_t* rdram, uint32_t guest_address, const UsbMouseRpcModel::Reply& reply) {
    uint8_t* destination = getMemPtr(rdram, guest_address);
    if (destination != nullptr) {
        std::memcpy(destination, reply.data(), reply.size());
    }
}

void MouseBind(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    ReturnToCaller(ctx, 0);
}

void MouseGetInfo(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_mouse_mutex);
    SampleHostMouse(rdram);
    WriteReply(rdram, getRegU32(ctx, kRegA0), g_mouse_model.InfoReply());
    ReturnToCaller(ctx, 0);
}

void MouseRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_mouse_mutex);
    const bool first_port = getRegU32(ctx, kRegA0) == 0;
    WriteReply(rdram, getRegU32(ctx, kRegA1), first_port ? g_mouse_model.ReadReply() : UsbMouseRpcModel::Reply{});
    ReturnToCaller(ctx, 0);
}

void MouseDeviceInfo(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    std::lock_guard<std::mutex> lock(g_mouse_mutex);
    WriteReply(rdram, getRegU32(ctx, kRegA1), g_mouse_model.DeviceInfoReply());
    ReturnToCaller(ctx, 0);
}

} // namespace

namespace aoe2::game_hooks {
void SetMousePresentationSource(const PS2Runtime& runtime) {
    g_presentation_runtime.store(&runtime, std::memory_order_release);
}

void RegisterMouseHandlers(KcetHandlers& handlers) {
    handlers.emplace("kcet.mouse.bind", MouseBind);
    handlers.emplace("kcet.mouse.get_info", MouseGetInfo);
    handlers.emplace("kcet.mouse.read", MouseRead);
    handlers.emplace("kcet.mouse.device_info", MouseDeviceInfo);
}
}
