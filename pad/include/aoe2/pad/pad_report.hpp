#pragma once

#include <array>
#include <cstdint>

namespace aoe2::pad {

enum PadButton : uint16_t {
    kSelect = 0x0001,
    kL3 = 0x0002,
    kR3 = 0x0004,
    kStart = 0x0008,
    kUp = 0x0010,
    kRight = 0x0020,
    kDown = 0x0040,
    kLeft = 0x0080,
    kL2 = 0x0100,
    kR2 = 0x0200,
    kL1 = 0x0400,
    kR1 = 0x0800,
    kTriangle = 0x1000,
    kCircle = 0x2000,
    kCross = 0x4000,
    kSquare = 0x8000,
};

struct HostPadState {
    bool connected;
    uint16_t buttons;
    float right_x;
    float right_y;
    float left_x;
    float left_y;
};

enum class PadMode : uint8_t {
    kDigital = 0x41,
    kAnalog = 0x73,
    kPressure = 0x79,
};

struct PadReport {
    std::array<uint8_t, 32> bytes{};
    int length = 0;
};

uint8_t AxisToByte(float value);
PadReport BuildReport(const HostPadState& state, PadMode mode);

}
