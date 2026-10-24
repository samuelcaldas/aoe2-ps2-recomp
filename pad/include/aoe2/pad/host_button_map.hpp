#pragma once
#include <array>
#include <cstdint>
namespace aoe2::pad {
enum class HostButton { kCross, kCircle, kSquare, kTriangle, kL1, kR1, kL2, kR2, kSelect, kStart, kL3, kR3, kUp, kDown, kLeft, kRight };
uint16_t ToPadButtons(const std::array<bool, 16>& down_by_host_button);
}
