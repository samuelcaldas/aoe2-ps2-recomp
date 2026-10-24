#include <aoe2/pad/pad_report.hpp>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

void axis_values_are_clamped_and_centered() {
    using aoe2::pad::AxisToByte;
    Check(AxisToByte(-1.0f) == 0, "negative axis endpoint");
    Check(AxisToByte(0.0f) == 128, "axis center");
    Check(AxisToByte(1.0f) == 255, "positive axis endpoint");
    Check(AxisToByte(2.0f) == 255, "axis upper clamp");
    Check(AxisToByte(-3.0f) == 0, "axis lower clamp");
    Check(AxisToByte(std::numeric_limits<float>::quiet_NaN()) == 128, "NaN axis center");
}

void digital_report_has_active_low_buttons_and_length() {
    using namespace aoe2::pad;
    HostPadState state{true, 0, 0.0f, 0.0f, 0.0f, 0.0f};
    const PadReport report = BuildReport(state, PadMode::kDigital);
    Check(report.length == 4 && report.bytes[1] == 0x41, "digital mode and length");
    Check(report.bytes[2] == 0xff && report.bytes[3] == 0xff, "released buttons are active-low");
    state.buttons = kCross;
    const PadReport cross = BuildReport(state, PadMode::kDigital);
    Check(cross.bytes[2] == 0xff && cross.bytes[3] == 0xbf, "cross clears only its bit");
    state.buttons = kSelect;
    const PadReport select = BuildReport(state, PadMode::kDigital);
    Check(select.bytes[2] == 0xfe && select.bytes[3] == 0xff, "select clears low-byte bit");
    for (std::size_t index = 4; index < 32; ++index)
        Check(report.bytes[index] == 0, "digital trailing bytes are zero");
}

void analog_report_places_axes_in_libpad_order() {
    using namespace aoe2::pad;
    const HostPadState state{true, 0, -1.0f, 0.0f, 1.0f, 1.0f};
    const PadReport report = BuildReport(state, PadMode::kAnalog);
    Check(report.length == 8 && report.bytes[4] == 0 && report.bytes[5] == 128 &&
          report.bytes[6] == 255 && report.bytes[7] == 255, "analog axis order");
    for (std::size_t index = 8; index < 32; ++index)
        Check(report.bytes[index] == 0, "analog trailing bytes are zero");
}

void pressure_report_maps_buttons_to_pressure_indices() {
    using namespace aoe2::pad;
    const HostPadState state{true, static_cast<uint16_t>(kTriangle | kUp), 0, 0, 0, 0};
    const PadReport report = BuildReport(state, PadMode::kPressure);
    Check(report.length == 20, "pressure length");
    for (std::size_t index = 8; index < 20; ++index) {
        const bool pressed = index == 10 || index == 12;
        Check(report.bytes[index] == (pressed ? 0xff : 0), "pressure index mapping");
    }
}

void disconnected_report_is_entirely_zero() {
    const aoe2::pad::PadReport report = aoe2::pad::BuildReport(
        {false, 0xffff, 1, 1, 1, 1}, aoe2::pad::PadMode::kPressure);
    Check(report.length == 0, "disconnected report length");
    for (uint8_t byte : report.bytes)
        Check(byte == 0, "disconnected report bytes");
}
}

int main() {
    axis_values_are_clamped_and_centered();
    digital_report_has_active_low_buttons_and_length();
    analog_report_places_axes_in_libpad_order();
    pressure_report_maps_buttons_to_pressure_indices();
    disconnected_report_is_entirely_zero();
}
