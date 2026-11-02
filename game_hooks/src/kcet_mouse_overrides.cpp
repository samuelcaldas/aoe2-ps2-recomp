// KCET host input replacements; guest addresses are loaded from local configuration.
#include <aoe2/game_hooks/kcet_handlers.hpp>
#if defined(AOE2_EXPERIMENTAL_MAP_VIEW)
#include <aoe2/game_hooks/map_view_config.hpp>
#include <aoe2/input/map_input_router.hpp>
#endif
#include <aoe2/input/presentation_transform.hpp>
#include <aoe2/input/switch_hid_geometry.hpp>
#include <aoe2/input/touch_gestures.hpp>
#include <aoe2/input/usb_mouse_rpc.hpp>
#include "input_synthetic.hpp"
#include "native_input_internal.hpp"
#include "raylib.h"
#include "runtime/ps2_memory.h"

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
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
std::atomic<bool> g_cursor_visible{true};

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

// Extent of the coordinate space reported by the active host pointer.
Vector2 WindowDimensions() {
#if defined(__SWITCH__)
    // libnx/Ryujinx absolute HID mouse coordinates use the 1280x720 panel
    // domain even when the emulated applet renders a docked 1920x1080 frame.
    return {aoe2::input::kSwitchHidCoordinateExtent.width,
            aoe2::input::kSwitchHidCoordinateExtent.height};
#else
    const float w = static_cast<float>(GetScreenWidth());
    const float h = static_cast<float>(GetScreenHeight());
    if (w > 0.0f && h > 0.0f) return {w, h};
    return {1920.0f, 1080.0f};
#endif
}

// The game's 640x448 screen fills whatever frame the runtime draws (640x512 once PAL display is set).
aoe2::input::PresentationTransform CurrentPresentation() {
    uint32_t width = 0;
    uint32_t height = 0;
    if (const PS2Runtime* runtime = g_presentation_runtime.load(std::memory_order_acquire)) runtime->presentedFrameExtent(width, height);
    const aoe2::input::Extent frame = width != 0 && height != 0
        ? aoe2::input::Extent{static_cast<float>(width), static_cast<float>(height)}
        : aoe2::input::Extent{kGuestScreenWidth, kGuestScreenHeight};
    const Vector2 win = WindowDimensions();
    return aoe2::input::PresentationTransform::LogicalFillsFrame(
        {win.x, win.y}, frame,
        {kGuestScreenWidth, kGuestScreenHeight});
}

// Absolute pointing: the host pointer's place in the window is where the game's cursor should be.
void SteerGuestCursor(uint8_t* rdram, Vector2 position, uint32_t buttons) {
    try {
#if defined(AOE2_EXPERIMENTAL_MAP_VIEW)
        const PS2Runtime* runtime = g_presentation_runtime.load(std::memory_order_acquire);
        auto active_layout = runtime ? aoe2::game_hooks::ActiveMapViewLayout(const_cast<PS2Runtime&>(*runtime))
                                     : aoe2::game_hooks::ActiveMapViewLayout();
        static std::optional<aoe2::input::MapInputRouter> router;
        static float last_layout_w = 0;
        static float last_layout_h = 0;
        const Vector2 win = WindowDimensions();
        if (active_layout) {
            if (!router || last_layout_w != active_layout->output.width || last_layout_h != active_layout->output.height) {
                router.emplace(*active_layout);
                router->ResetMouse(buttons);
                last_layout_w = active_layout->output.width;
                last_layout_h = active_layout->output.height;
            }
            const aoe2::input::Extent win_extent{win.x, win.y};
            auto routed = router->RouteMouse(position.x, position.y, win_extent, buttons);
            if (routed.point) {
                const int32_t cursor_x = LoadGuestInt(rdram, kGuestCursorX);
                const int32_t cursor_y = LoadGuestInt(rdram, kGuestCursorY);
                aoe2::input::GuestPoint target{0.0f, 0.0f};
                if (routed.point->region == aoe2::input::MapInputRegion::Map) {
                    target.x = routed.point->coordinates.x;
                    target.y = active_layout->map.y + routed.point->coordinates.y;
                } else {
                    target = routed.point->coordinates;
                }
                LogPointer(position, target, cursor_x, cursor_y);
                g_mouse_model.SteerTo(target.x - static_cast<float>(cursor_x),
                                     target.y - static_cast<float>(cursor_y), routed.buttons);
                return;
            }
            g_mouse_model.SteerTo(0, 0, 0);
            return;
        }
        router.reset();
#endif

        const auto target = CurrentPresentation().ToLogical(position.x, position.y);
        const int32_t cursor_x = LoadGuestInt(rdram, kGuestCursorX);
        const int32_t cursor_y = LoadGuestInt(rdram, kGuestCursorY);
        LogPointer(position, target, cursor_x, cursor_y);
        g_mouse_model.SteerTo(target.x - static_cast<float>(cursor_x), target.y - static_cast<float>(cursor_y), buttons);
    } catch (const std::exception& e) {
        std::cerr << "[host-mouse] exception in SteerGuestCursor: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[host-mouse] unknown exception in SteerGuestCursor" << std::endl;
    }
}

#if defined(__SWITCH__)
aoe2::input::GuestPoint g_switch_touch_position{};
uint32_t g_switch_touch_held_buttons = 0;
uint32_t g_switch_touch_reported_buttons = 0;
struct SwitchTouchMouseSample {
    aoe2::input::GuestPoint position;
    uint32_t buttons;
};
std::deque<SwitchTouchMouseSample> g_switch_touch_button_transitions;

class SwitchTouchMouseSink final : public aoe2::input::InputSink {
public:
    void SubmitEvent(const aoe2::input::ActionEvent& event) override {
        g_switch_touch_position = {event.x, event.y};
        switch (event.type) {
            case aoe2::input::ActionType::PointerDown:
                g_switch_touch_held_buttons |= aoe2::input::kMouseButtonLeft;
                Queue(g_switch_touch_held_buttons);
                break;
            case aoe2::input::ActionType::PointerUp:
                g_switch_touch_held_buttons &= ~aoe2::input::kMouseButtonLeft;
                Queue(g_switch_touch_held_buttons);
                break;
            case aoe2::input::ActionType::SecondaryDown:
                g_switch_touch_held_buttons |= aoe2::input::kMouseButtonRight;
                Queue(g_switch_touch_held_buttons);
                break;
            case aoe2::input::ActionType::SecondaryUp:
                g_switch_touch_held_buttons &= ~aoe2::input::kMouseButtonRight;
                Queue(g_switch_touch_held_buttons);
                break;
            case aoe2::input::ActionType::PrimaryClick:
                Queue(g_switch_touch_held_buttons | aoe2::input::kMouseButtonLeft);
                Queue(g_switch_touch_held_buttons);
                break;
            case aoe2::input::ActionType::SecondaryClick:
                Queue(g_switch_touch_held_buttons | aoe2::input::kMouseButtonRight);
                Queue(g_switch_touch_held_buttons);
                break;
            case aoe2::input::ActionType::DoubleClick:
                for (int click = 0; click < 2; ++click) {
                    Queue(g_switch_touch_held_buttons | aoe2::input::kMouseButtonLeft);
                    Queue(g_switch_touch_held_buttons);
                }
                break;
            default:
                break;
        }
    }

private:
    static void Queue(uint32_t buttons) {
        g_switch_touch_button_transitions.push_back(
            SwitchTouchMouseSample{g_switch_touch_position, buttons});
    }
};

SwitchTouchMouseSink g_switch_touch_sink;
aoe2::input::TouchGestureTranslator g_switch_touch_translator(
    g_switch_touch_sink,
    aoe2::input::SurfaceMapping({1280.0f, 720.0f}, {1280.0f, 720.0f}));
bool g_switch_touch_active = false;
bool g_switch_touch_multitouch_blocked = false;
uint32_t g_switch_touch_finger_id = 0;

uint64_t SwitchTouchTimestampUs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

SwitchTouchMouseSample NextSwitchTouchSample() {
    if (g_switch_touch_button_transitions.empty()) {
        return {g_switch_touch_position, g_switch_touch_reported_buttons};
    }
    const SwitchTouchMouseSample sample = g_switch_touch_button_transitions.front();
    g_switch_touch_button_transitions.pop_front();
    g_switch_touch_reported_buttons = sample.buttons;
    return sample;
}
#endif
}  // namespace

namespace aoe2::game_hooks::detail {
void SampleHostMouse(uint8_t* rdram) {
    try {
        if (aoe2::game_hooks::synthetic::Enabled()) return SampleSyntheticMouse();
#if defined(__SWITCH__)
        static bool s_hid_mouse_init = false;
        if (!s_hid_mouse_init) {
            hidInitializeMouse();
            hidInitializeTouchScreen();
            s_hid_mouse_init = true;
        }
        HidMouseState mouse_states[1];
        size_t mouse_count = hidGetMouseStates(mouse_states, 1);

        HidTouchScreenState touch_states[1];
        size_t touch_count = hidGetTouchScreenStates(touch_states, 1);

        const uint64_t touch_timestamp_us = SwitchTouchTimestampUs();
        const bool has_touch_state = touch_count > 0;
        const int active_touches = has_touch_state
            ? std::clamp(touch_states[0].count, 0, 16) : 0;
        if (active_touches > 1) {
            if (g_switch_touch_active) {
                g_switch_touch_translator.Submit({g_switch_touch_finger_id,
                    g_switch_touch_position.x, g_switch_touch_position.y,
                    aoe2::input::TouchPhase::Cancelled, touch_timestamp_us});
            }
            g_switch_touch_active = false;
            g_switch_touch_multitouch_blocked = true;
        } else if (active_touches == 1 && !g_switch_touch_multitouch_blocked) {
            const HidTouchState& touch = touch_states[0].touches[0];
            const auto phase = g_switch_touch_active &&
                    g_switch_touch_finger_id == touch.finger_id
                ? aoe2::input::TouchPhase::Moved
                : aoe2::input::TouchPhase::Began;
            if (g_switch_touch_active && phase == aoe2::input::TouchPhase::Began) {
                g_switch_touch_translator.Submit({g_switch_touch_finger_id,
                    g_switch_touch_position.x, g_switch_touch_position.y,
                    aoe2::input::TouchPhase::Cancelled, touch_timestamp_us});
            }
            g_switch_touch_finger_id = touch.finger_id;
            g_switch_touch_position = {static_cast<float>(touch.x), static_cast<float>(touch.y)};
            g_switch_touch_translator.Submit({touch.finger_id,
                static_cast<float>(touch.x), static_cast<float>(touch.y),
                phase, touch_timestamp_us});
            g_switch_touch_active = true;
        } else if (active_touches == 0) {
            if (g_switch_touch_active) {
                g_switch_touch_translator.Submit({g_switch_touch_finger_id,
                    g_switch_touch_position.x, g_switch_touch_position.y,
                    aoe2::input::TouchPhase::Ended, touch_timestamp_us});
                g_switch_touch_active = false;
            }
            g_switch_touch_multitouch_blocked = false;
        }

        if (g_switch_touch_active || !g_switch_touch_button_transitions.empty() ||
            (has_touch_state && active_touches == 0 && g_switch_touch_reported_buttons != 0)) {
            g_mouse_model.SetConnected(true);
            const SwitchTouchMouseSample touch_sample = NextSwitchTouchSample();
            const uint32_t touch_buttons = touch_sample.buttons;
            const Vector2 touch_position{touch_sample.position.x, touch_sample.position.y};
            LogButtonChange(touch_buttons);
            if (rdram != nullptr) {
                SteerGuestCursor(rdram, touch_position, touch_buttons);
            } else {
                const Vector2 last = g_has_last_position ? g_last_position : touch_position;
                g_last_position = touch_position;
                g_has_last_position = true;
                g_mouse_model.Submit(HostMouseReport{
                    touch_position.x - last.x, touch_position.y - last.y,
                    touch_buttons, 0, 0.0f});
            }
            return;
        }

        Vector2 position{0.0f, 0.0f};
        uint32_t buttons = 0;
        bool has_input = false;

        if (touch_count > 0 && touch_states[0].count > 0) {
            position.x = static_cast<float>(touch_states[0].touches[0].x);
            position.y = static_cast<float>(touch_states[0].touches[0].y);
            buttons |= aoe2::input::kMouseButtonLeft;
            has_input = true;
        } else if (mouse_count > 0) {
            position.x = static_cast<float>(mouse_states[0].x);
            position.y = static_cast<float>(mouse_states[0].y);
            if (mouse_states[0].buttons & HidMouseButton_Left) buttons |= aoe2::input::kMouseButtonLeft;
            if (mouse_states[0].buttons & HidMouseButton_Right) buttons |= aoe2::input::kMouseButtonRight;
            if (mouse_states[0].buttons & HidMouseButton_Middle) buttons |= aoe2::input::kMouseButtonMiddle;
            if (mouse_states[0].delta_x != 0 || mouse_states[0].delta_y != 0 || buttons != 0) {
                has_input = true;
            }
        }

        // Do not continually steer back to the last absolute pointer sample:
        // the Switch right stick feeds relative mouse deltas through the same
        // RPC queue and must be able to leave the cursor at its new position.
        if (!has_input) {
            // Release buttons when touch/mouse input ends without steering the
            // cursor back to the last absolute sample.
            g_mouse_model.Submit(HostMouseReport{0.0f, 0.0f, 0, 0, 0.0f});
            return;
        }

        g_mouse_model.SetConnected(true);
        LogButtonChange(buttons);
        const Vector2 last = g_has_last_position ? g_last_position : position;
        g_last_position = position;
        g_has_last_position = true;
        if (rdram != nullptr) return SteerGuestCursor(rdram, position, buttons);
        g_mouse_model.Submit(HostMouseReport{position.x - last.x, position.y - last.y, buttons, 0, 0.0f});
#else
        const Vector2 position = GetMousePosition();
        const uint32_t buttons = HostButtons();
        g_mouse_model.SetConnected(true);
        LogButtonChange(buttons);
        const Vector2 last = g_has_last_position ? g_last_position : position;
        g_last_position = position;
        g_has_last_position = true;
        if (rdram != nullptr && IsCursorOnScreen()) return SteerGuestCursor(rdram, position, buttons);
        g_mouse_model.Submit(HostMouseReport{position.x - last.x, position.y - last.y, buttons, 0, GetMouseWheelMove()});
#endif
    } catch (const std::exception& e) {
        std::cerr << "[host-mouse] exception in SampleHostMouse: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[host-mouse] unknown exception in SampleHostMouse" << std::endl;
    }
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

void SetMouseCursorVisible(bool visible) {
    g_cursor_visible.store(visible, std::memory_order_release);
}

bool IsMouseCursorVisible() {
    return g_cursor_visible.load(std::memory_order_acquire);
}

void RegisterMouseHandlers(KcetHandlers& handlers) {
    handlers.emplace("kcet.mouse.bind", MouseBind);
    handlers.emplace("kcet.mouse.get_info", MouseGetInfo);
    handlers.emplace("kcet.mouse.read", MouseRead);
    handlers.emplace("kcet.mouse.device_info", MouseDeviceInfo);
}
}
