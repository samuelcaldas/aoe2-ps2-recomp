// Host gamepad backend for the native pad device (raylib 5.5, gamepad 0).
//
// Button layout follows raylib's logical names: the bottom face button is Cross, right is Circle, left is
// Square, top is Triangle. Triggers count as L2/R2 when pulled past half travel (raylib reports -1..1).
// Rumble: raylib has no "stop" call; zero strength with zero duration stops it. The device only calls
// Rumble when the value changes, so the backend re-applies a running vibration on each poll, because a
// raylib vibration lasts a fixed duration.
#include <aoe2/game_hooks/kcet_pad_backend.hpp>
#include <aoe2/pad/host_button_map.hpp>

#include <raylib.h>
#include "pad_snapshot.hpp"

#if defined(__SWITCH__)
#include <switch.h>
#endif

#include <array>
#include <chrono>
#include <iostream>

namespace aoe2::game_hooks {
namespace {
using aoe2::pad::HostPadState;
using Clock = std::chrono::steady_clock;

constexpr float kVibrationSeconds = 0.2f;
constexpr auto kReapplyInterval = std::chrono::milliseconds(150);
constexpr float kTriggerThreshold = 0.5f;

// Index = aoe2::pad::HostButton.
constexpr std::array<int, 16> kRaylibButtons = {
    GAMEPAD_BUTTON_RIGHT_FACE_DOWN, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, GAMEPAD_BUTTON_RIGHT_FACE_LEFT,
    GAMEPAD_BUTTON_RIGHT_FACE_UP,   GAMEPAD_BUTTON_LEFT_TRIGGER_1,   GAMEPAD_BUTTON_RIGHT_TRIGGER_1,
    GAMEPAD_BUTTON_LEFT_TRIGGER_2,  GAMEPAD_BUTTON_RIGHT_TRIGGER_2,  GAMEPAD_BUTTON_MIDDLE_LEFT,
    GAMEPAD_BUTTON_MIDDLE_RIGHT,    GAMEPAD_BUTTON_LEFT_THUMB,       GAMEPAD_BUTTON_RIGHT_THUMB,
    GAMEPAD_BUTTON_LEFT_FACE_UP,    GAMEPAD_BUTTON_LEFT_FACE_DOWN,   GAMEPAD_BUTTON_LEFT_FACE_LEFT,
    GAMEPAD_BUTTON_LEFT_FACE_RIGHT};

// Keyboard fallback when no gamepad is connected. Index = aoe2::pad::HostButton, so the layout matches
// kRaylibButtons (Enter/Space stay keyboard-only: a pad Cross posts a virtual click that swallows the key): Cross, Circle, Square, Triangle, L1, R1, L2, R2, Select, Start, L3, R3, Up, Down, Left, Right.
constexpr std::array<std::array<int, 2>, 16> kKeyboardButtons = {{
    {KEY_Z, KEY_NULL},    {KEY_X, KEY_NULL},     {KEY_A, KEY_NULL},     {KEY_S, KEY_BACKSPACE},
    {KEY_Q, KEY_NULL},    {KEY_W, KEY_NULL},     {KEY_E, KEY_NULL},     {KEY_R, KEY_NULL},
    {KEY_TAB, KEY_NULL},  {KEY_SPACE, KEY_NULL}, {KEY_NULL, KEY_NULL},  {KEY_NULL, KEY_NULL},
    {KEY_UP, KEY_NULL},   {KEY_DOWN, KEY_NULL},  {KEY_LEFT, KEY_NULL},  {KEY_RIGHT, KEY_NULL}}};

bool KeyboardButtonDown(std::size_t index) {
    for (const int key : kKeyboardButtons[index])
        if (key != KEY_NULL && IsKeyDown(key)) return true;
    return false;
}

class RaylibPadBackend final : public aoe2::pad::PadBackend {
public:
    HostPadState Sample(int port) override {
        if (port != 0) return {};
#if defined(__SWITCH__)
        static PadState s_switchPad;
        static bool s_switchPadInit = false;
        if (!s_switchPadInit) {
            padConfigureInput(1, HidNpadStyleSet_NpadStandard);
            padInitializeAny(&s_switchPad);
            s_switchPadInit = true;
        }
        padUpdate(&s_switchPad);
        const uint64_t keys = padGetButtons(&s_switchPad);
        const HidAnalogStickState lstick = padGetStickPos(&s_switchPad, 0);
        const HidAnalogStickState rstick = padGetStickPos(&s_switchPad, 1);

        std::array<bool, 16> down{};
        down[static_cast<size_t>(aoe2::pad::HostButton::kCross)] = (keys & HidNpadButton_B) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kCircle)] = (keys & HidNpadButton_A) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kSquare)] = (keys & HidNpadButton_Y) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kTriangle)] = (keys & HidNpadButton_X) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kL1)] = (keys & HidNpadButton_L) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kR1)] = (keys & HidNpadButton_R) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kL2)] = (keys & HidNpadButton_ZL) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kR2)] = (keys & HidNpadButton_ZR) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kSelect)] = (keys & HidNpadButton_Minus) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kStart)] = (keys & HidNpadButton_Plus) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kL3)] = (keys & HidNpadButton_StickL) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kR3)] = (keys & HidNpadButton_StickR) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kUp)] = (keys & HidNpadButton_Up) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kDown)] = (keys & HidNpadButton_Down) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kLeft)] = (keys & HidNpadButton_Left) != 0;
        down[static_cast<size_t>(aoe2::pad::HostButton::kRight)] = (keys & HidNpadButton_Right) != 0;

        float rx = static_cast<float>(rstick.x) / 32767.0f;
        float ry = -static_cast<float>(rstick.y) / 32767.0f;
        float lx = static_cast<float>(lstick.x) / 32767.0f;
        float ly = -static_cast<float>(lstick.y) / 32767.0f;
        return {true, aoe2::pad::ToPadButtons(down), rx, ry, lx, ly};
#else
        if (!IsGamepadAvailable(0)) return SampleKeyboard();
        ReapplyVibration();
        std::array<bool, 16> down{};
        for (std::size_t i = 0; i < down.size(); ++i)
            down[i] = IsGamepadButtonDown(0, kRaylibButtons[i]);
        constexpr std::size_t kL2 = static_cast<std::size_t>(aoe2::pad::HostButton::kL2);
        constexpr std::size_t kR2 = static_cast<std::size_t>(aoe2::pad::HostButton::kR2);
        down[kL2] = down[kL2] || GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerThreshold;
        down[kR2] = down[kR2] || GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerThreshold;
        return {true,
                aoe2::pad::ToPadButtons(down),
                GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X),
                GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y),
                GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X),
                GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y)};
#endif
    }

    void Rumble(int port, uint8_t small_motor, uint8_t large_motor) override {
        if (port != 0 || !IsGamepadAvailable(0)) return;
        small_ = small_motor != 0;
        large_ = large_motor;
        active_ = small_ || large_ != 0;
        if (!active_) {
            SetGamepadVibration(0, 0.0f, 0.0f, 0.0f);
            return;
        }
        Apply();
    }

private:
    HostPadState SampleKeyboard() {
        std::array<bool, 16> down{};
        for (std::size_t i = 0; i < down.size(); ++i) down[i] = KeyboardButtonDown(i);
        const uint16_t buttons = aoe2::pad::ToPadButtons(down);
        if (buttons != last_keyboard_buttons_)
            std::cerr << "[host-pad] keyboard buttons=0x" << std::hex << buttons << std::dec << std::endl;
        last_keyboard_buttons_ = buttons;
        return {true, buttons, 0.0f, 0.0f, 0.0f, 0.0f};
    }

    void Apply() {
        SetGamepadVibration(0, large_ / 255.0f, small_ ? 1.0f : 0.0f, kVibrationSeconds);
        last_applied_ = Clock::now();
    }
    void ReapplyVibration() {
        if (active_ && Clock::now() - last_applied_ >= kReapplyInterval) Apply();
    }

    uint16_t last_keyboard_buttons_ = 0;
    bool small_ = false;
    uint8_t large_ = 0;
    bool active_ = false;
    Clock::time_point last_applied_{};
};

class SyntheticPadBackend final : public aoe2::pad::PadBackend {
public:
    // Snapshot comparisons use the outer slot step, so extra connectivity queries cannot advance input.
    // Without the observer, retain the standalone diagnostic backend's sample-count sequence.
    HostPadState Sample(int port) override {
        if (port >= 0 && port < 2) ++diagnostics::input_samples[port];
        if (port != 0) return {};
        const bool cross_down = diagnostics::SyntheticCrossDown(samples_++);
        return {true, static_cast<uint16_t>(cross_down ? static_cast<uint16_t>(aoe2::pad::PadButton::kCross) : 0u),
                0.0f, 0.0f, 0.0f, 0.0f};
    }
    void Rumble(int port, uint8_t small_motor, uint8_t large_motor) override {
        std::cerr << "[synthetic-pad] rumble port=" << port << " small=" << int(small_motor)
                  << " large=" << int(large_motor) << std::endl;
    }

private:
    uint64_t samples_ = 0;
};
}  // namespace

std::unique_ptr<aoe2::pad::PadBackend> MakeRaylibPadBackend() { return std::make_unique<RaylibPadBackend>(); }
std::unique_ptr<aoe2::pad::PadBackend> MakeSyntheticPadBackend() { return std::make_unique<SyntheticPadBackend>(); }
}  // namespace aoe2::game_hooks
