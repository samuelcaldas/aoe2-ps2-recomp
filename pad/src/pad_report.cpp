#include <aoe2/pad/pad_report.hpp>

#include <algorithm>
#include <cmath>

namespace aoe2::pad {
namespace {

constexpr int ReportLength(PadMode mode) {
    switch (mode) {
    case PadMode::kDigital:
        return 4;
    case PadMode::kAnalog:
        return 8;
    case PadMode::kPressure:
        return 20;
    }
    return 0;
}

void WriteButtons(PadReport& report, uint16_t pressed) {
    const uint16_t released = static_cast<uint16_t>(~pressed);
    report.bytes[2] = static_cast<uint8_t>(released & 0xff);
    report.bytes[3] = static_cast<uint8_t>(released >> 8);
}

void WriteAxes(PadReport& report, const HostPadState& state) {
    report.bytes[4] = AxisToByte(state.right_x);
    report.bytes[5] = AxisToByte(state.right_y);
    report.bytes[6] = AxisToByte(state.left_x);
    report.bytes[7] = AxisToByte(state.left_y);
}

constexpr std::array<uint16_t, 12> kPressureButtons = {
    kRight, kLeft, kUp, kDown, kTriangle, kCircle,
    kCross, kSquare, kL1, kL2, kR1, kR2,
};

void WritePressure(PadReport& report, uint16_t pressed) {
    for (std::size_t index = 0; index < kPressureButtons.size(); ++index)
        report.bytes[index + 8] = (pressed & kPressureButtons[index]) ? 0xff : 0;
}

}

uint8_t AxisToByte(float value) {
    if (std::isnan(value))
        return 128;
    const float bounded = std::clamp(value, -1.0f, 1.0f);
    return static_cast<uint8_t>(std::lround((bounded + 1.0f) * 127.5f));
}

RelativePointerDelta StickToPointerDelta(float x, float y, float elapsed_seconds) {
    constexpr float deadzone = 0.18f;
    constexpr float pointer_speed = 420.0f;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(elapsed_seconds) || elapsed_seconds <= 0.0f)
        return {};

    const float magnitude = std::min(1.0f, std::sqrt(x * x + y * y));
    if (magnitude <= deadzone) return {};

    const float active_magnitude = (magnitude - deadzone) / (1.0f - deadzone);
    const float scale = active_magnitude * pointer_speed * std::min(elapsed_seconds, 0.05f) / magnitude;
    return {x * scale, y * scale};
}

PadReport BuildReport(const HostPadState& state, PadMode mode) {
    PadReport report{};
    if (!state.connected)
        return report;
    report.length = ReportLength(mode);
    if (report.length == 0)
        return report;
    report.bytes[1] = static_cast<uint8_t>(mode);
    WriteButtons(report, state.buttons);
    if (mode == PadMode::kAnalog || mode == PadMode::kPressure)
        WriteAxes(report, state);
    if (mode == PadMode::kPressure)
        WritePressure(report, state.buttons);
    return report;
}

}
