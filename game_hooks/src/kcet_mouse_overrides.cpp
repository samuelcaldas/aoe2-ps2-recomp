// KCET host input replacements; guest addresses are loaded from local configuration.
#include <aoe2/game_hooks/kcet_handlers.hpp>
#include <aoe2/input/usb_mouse_rpc.hpp>
#include "input_synthetic.hpp"
#include "native_input_internal.hpp"
#include "raylib.h"
#include "runtime/ps2_memory.h"

#include <cstring>
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
}  // namespace

namespace aoe2::game_hooks::detail {
void SampleHostMouse() {
    if (aoe2::game_hooks::synthetic::Enabled()) return SampleSyntheticMouse();
    const Vector2 position = GetMousePosition();
    const Vector2 last = g_has_last_position ? g_last_position : position;
    g_last_position = position;
    g_has_last_position = true;
    g_mouse_model.SetConnected(true);
    g_mouse_model.Submit(HostMouseReport{position.x - last.x, position.y - last.y, HostButtons(), 0, GetMouseWheelMove()});
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
    SampleHostMouse();
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
void RegisterMouseHandlers(KcetHandlers& handlers) {
    handlers.emplace("kcet.mouse.bind", MouseBind);
    handlers.emplace("kcet.mouse.get_info", MouseGetInfo);
    handlers.emplace("kcet.mouse.read", MouseRead);
    handlers.emplace("kcet.mouse.device_info", MouseDeviceInfo);
}
}
