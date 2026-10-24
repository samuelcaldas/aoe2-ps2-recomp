// Deterministic synthetic mouse and keyboard input for runs without a human (AOE2_INPUT_SYNTHETIC=1).
//
// The input is a pure function of the LOGICAL STEP of the module: the number of completed per-frame
// updates (ktMouseProcessEvents / ktKeybdProcessEvents), not of how many RPC reads happened or of time.
// Original and native modules may issue a different number of RPC queries per update; the same logical
// step therefore always sees the same host input, which keeps snapshot comparisons meaningful.
#pragma once

#include <aoe2/input/usb_mouse.hpp>

#include <atomic>
#include <bitset>
#include <cstdint>
#include <cstdlib>

namespace aoe2::game_hooks::synthetic {

enum class Module : int { kMouse = 0, kKeyboard = 1 };

inline constexpr uint64_t kMouseMovePeriod = 50;     // a motion every 50 steps
inline constexpr uint64_t kMouseButtonPeriod = 200;  // left button toggles every 200 steps
inline constexpr float kMouseDeltaX = 3.0f;
inline constexpr float kMouseDeltaY = -2.0f;
inline constexpr uint64_t kKeyboardPeriod = 120;     // key A held for the first 30 steps of every 120
inline constexpr uint64_t kKeyboardHold = 30;
inline constexpr uint8_t kKeyboardUsageA = 0x04;

inline bool Enabled() {
    static const bool enabled = std::getenv("AOE2_INPUT_SYNTHETIC") != nullptr;
    return enabled;
}

inline std::atomic<uint64_t>& LogicalSteps(Module module) {
    static std::atomic<uint64_t> steps[2];
    return steps[static_cast<int>(module)];
}

// One completed per-frame update of the module; only counted when synthetic input is on.
inline void NoteUpdate(Module module) {
    if (Enabled()) ++LogicalSteps(module);
}

// Left button held for steps [200, 400), [600, 800), ...
inline bool MouseButtonDown(uint64_t step) { return (step / kMouseButtonPeriod) % 2 == 1; }

class SyntheticMouse {
public:
    // The motion for `step` is reported once, however many times the step is sampled.
    aoe2::input::HostMouseReport Next(uint64_t step) {
        aoe2::input::HostMouseReport report;
        report.buttons = MouseButtonDown(step) ? aoe2::input::kMouseButtonLeft : 0u;
        const bool move = step > 0 && step % kMouseMovePeriod == 0 && step != last_motion_step_;
        if (move) {
            report.delta_x = kMouseDeltaX;
            report.delta_y = kMouseDeltaY;
            last_motion_step_ = step;
        }
        return report;
    }

private:
    uint64_t last_motion_step_ = UINT64_MAX;
};

// Held HID usages at `step`.
inline std::bitset<256> KeyboardState(uint64_t step) {
    std::bitset<256> keys;
    if (step % kKeyboardPeriod < kKeyboardHold) keys.set(kKeyboardUsageA);
    return keys;
}

}  // namespace aoe2::game_hooks::synthetic
