#include <aoe2/pad/host_button_map.hpp>
namespace aoe2::pad {
uint16_t ToPadButtons(const std::array<bool, 16>& down_by_host_button) {
    constexpr uint16_t bits[] = {0x4000,0x2000,0x8000,0x1000,0x0400,0x0800,0x0100,0x0200,0x0001,0x0008,0x0002,0x0004,0x0010,0x0040,0x0080,0x0020};
    uint16_t result = 0;
    for (std::size_t index = 0; index < down_by_host_button.size(); ++index) {
        if (down_by_host_button[index]) result |= bits[index];
    }
    return result;
}
}
