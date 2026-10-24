#include "game_overrides.h"
#undef PS2_REGISTER_GAME_OVERRIDE
#define PS2_REGISTER_GAME_OVERRIDE(...)
#include "../../game_hooks/src/pad_snapshot.hpp"
using aoe2::game_hooks::diagnostics::LogSlotSnapshot;

#include <vector>

int main() {
    unsetenv("AOE2_PAD_SNAPSHOT");
    std::vector<uint8_t> ram(PS2_RAM_SIZE + 0x200, 0xA5);
    LogSlotSnapshot(ram.data(), PS2_RAM_SIZE - 0x200, 0, 1); // exact end
    LogSlotSnapshot(ram.data(), PS2_RAM_SIZE - 16, 0, 2); // crosses RAM end
    LogSlotSnapshot(ram.data(), UINT32_MAX, 0, 3); // wraps address range
    LogSlotSnapshot(nullptr, 0, 0, 4);
    aoe2::game_hooks::diagnostics::LogPadStep(ram.data(), 0x1000); // disabled
    setenv("AOE2_PAD_SNAPSHOT", "1", 1);
    uint32_t slots = 0x1000, count = 2;
    std::memcpy(ram.data() + 0x004EA148, &slots, 4);
    std::memcpy(ram.data() + 0x004EA14C, &count, 4);
    aoe2::game_hooks::diagnostics::input_samples = {17, 9};
    aoe2::game_hooks::diagnostics::LogPadStep(ram.data(), slots);
    aoe2::game_hooks::diagnostics::LogPadStep(ram.data(), slots + 0x200);
    aoe2::game_hooks::diagnostics::LogPadStep(ram.data(), slots + 0x400); // beyond count
    aoe2::game_hooks::diagnostics::LogPadStep(ram.data(), slots - 0x200); // underflow
    aoe2::game_hooks::diagnostics::LogPadStep(ram.data(), slots + 1); // unaligned
    using namespace aoe2::game_hooks::diagnostics;
    for (int i = 0; i < 100; ++i) {
        BeginPadStep(ram.data(), slots);
        if (!SyntheticCrossDown(i) || !SyntheticCrossDown(999)) return 1;
    }
    BeginPadStep(ram.data(), slots + 0x400); // invalid index must not advance either input
    if (logical_steps[0] != 100 || logical_steps[1] != 0) return 2;
    BeginPadStep(ram.data(), slots);
    if (SyntheticCrossDown(0) || SyntheticCrossDown(999)) return 3;
    unsetenv("AOE2_PAD_SNAPSHOT");
    BeginPadStep(ram.data(), slots);
    if (logical_steps[0] != 101) return 4;
    setenv("AOE2_PAD_SNAPSHOT", "1", 1);
    for (int i = 0; i < 64; ++i)
        aoe2::game_hooks::diagnostics::LogPadStep(ram.data(), slots);
}
