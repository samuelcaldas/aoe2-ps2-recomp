#pragma once

#include "aoe2/input/actions.hpp"
#include "aoe2/input/touch_gestures.hpp"
#include "aoe2/input/surface_mapping.hpp"
#include "aoe2/platform/switch/switch_platform.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace aoe2::platform::switch_env {

// PS2 DualShock 2 button masks (matching kt_libpad)
namespace ps2_pad {
constexpr uint16_t SELECT   = 1 << 0;
constexpr uint16_t L3       = 1 << 1;
constexpr uint16_t R3       = 1 << 2;
constexpr uint16_t START    = 1 << 3;
constexpr uint16_t UP       = 1 << 4;
constexpr uint16_t RIGHT    = 1 << 5;
constexpr uint16_t DOWN     = 1 << 6;
constexpr uint16_t LEFT     = 1 << 7;
constexpr uint16_t L2       = 1 << 8;
constexpr uint16_t R2       = 1 << 9;
constexpr uint16_t L1       = 1 << 10;
constexpr uint16_t R1       = 1 << 11;
constexpr uint16_t TRIANGLE = 1 << 12;
constexpr uint16_t CIRCLE   = 1 << 13;
constexpr uint16_t CROSS    = 1 << 14;
constexpr uint16_t SQUARE   = 1 << 15;
} // namespace ps2_pad

// Abstract Joy-Con / Switch Pad input mask
namespace switch_btn {
constexpr uint64_t A        = 1ULL << 0;
constexpr uint64_t B        = 1ULL << 1;
constexpr uint64_t X        = 1ULL << 2;
constexpr uint64_t Y        = 1ULL << 3;
constexpr uint64_t LSTICK   = 1ULL << 4;
constexpr uint64_t RSTICK   = 1ULL << 5;
constexpr uint64_t L        = 1ULL << 6;
constexpr uint64_t R        = 1ULL << 7;
constexpr uint64_t ZL       = 1ULL << 8;
constexpr uint64_t ZR       = 1ULL << 9;
constexpr uint64_t PLUS     = 1ULL << 10;
constexpr uint64_t MINUS    = 1ULL << 11;
constexpr uint64_t DLEFT    = 1ULL << 12;
constexpr uint64_t DUP      = 1ULL << 13;
constexpr uint64_t DRIGHT   = 1ULL << 14;
constexpr uint64_t DDOWN    = 1ULL << 15;
} // namespace switch_btn

struct SwitchTouchPoint {
    uint32_t id{0};
    float x{0.0f};
    float y{0.0f};
    aoe2::input::TouchPhase phase{aoe2::input::TouchPhase::Began};
    uint64_t timestamp_us{0};
};

class ActionCollectorSink : public aoe2::input::InputSink {
public:
    void SubmitEvent(const aoe2::input::ActionEvent& event) override {
        events.push_back(event);
    }
    std::vector<aoe2::input::ActionEvent> events;
};

class SwitchInputBridge {
public:
    explicit SwitchInputBridge(aoe2::input::InputSink* target_sink = nullptr);

    // Maps Switch Pad buttons to PS2 DualShock 2 buttons
    static uint16_t MapButtonsToDualShock2(uint64_t switch_keys_down);

    // Converts Switch touch events into aoe2 action events
    std::vector<aoe2::input::ActionEvent> ProcessTouch(
        const SwitchTouchPoint& touch,
        ConsoleOperationMode mode = ConsoleOperationMode::Handheld);

    // Direct mapping utility from Switch screen coords to Guest 640x480 coords
    aoe2::input::GuestPoint MapTouchToGuest(
        float host_x, float host_y,
        ConsoleOperationMode mode = ConsoleOperationMode::Handheld) const;

    // Gesture and selection mode configuration
    void SetSelectionActive(bool active);
    void SetTouchRegion(aoe2::input::TouchRegion region);
    void SetOneFingerPan(bool enable);
    void SetModifierState(uint64_t switch_keys_down);

    aoe2::input::TouchGestureTranslator& HandheldTranslator() { return *handheld_translator_; }
    aoe2::input::TouchGestureTranslator& DockedTranslator() { return *docked_translator_; }

private:
    aoe2::input::SurfaceMapping handheld_mapping_{{1280.0f, 720.0f}, {640.0f, 480.0f}};
    aoe2::input::SurfaceMapping docked_mapping_{{1920.0f, 1080.0f}, {640.0f, 480.0f}};
    ActionCollectorSink default_sink_;
    aoe2::input::InputSink* active_sink_{nullptr};
    std::unique_ptr<aoe2::input::TouchGestureTranslator> handheld_translator_;
    std::unique_ptr<aoe2::input::TouchGestureTranslator> docked_translator_;
};

} // namespace aoe2::platform::switch_env
