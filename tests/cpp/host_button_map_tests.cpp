#include <aoe2/pad/host_button_map.hpp>
#include <cassert>
#include <array>
int main() {
    std::array<bool, 16> buttons{};
    assert(aoe2::pad::ToPadButtons(buttons) == 0);
    buttons[static_cast<int>(aoe2::pad::HostButton::kCross)] = true;
    assert(aoe2::pad::ToPadButtons(buttons) == 0x4000);
    buttons.fill(true);
    assert(aoe2::pad::ToPadButtons(buttons) == 0xFFFF);
    for (int i = 0; i < 16; ++i) {
        buttons.fill(false);
        buttons[i] = true;
        constexpr uint16_t expected[] = {0x4000,0x2000,0x8000,0x1000,0x0400,0x0800,0x0100,0x0200,0x0001,0x0008,0x0002,0x0004,0x0010,0x0040,0x0080,0x0020};
        assert(aoe2::pad::ToPadButtons(buttons) == expected[i]);
    }
}
