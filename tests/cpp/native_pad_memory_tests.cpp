// Exercise the actual runtime adapter without installing its override handlers.
#include "game_overrides.h"
#undef PS2_REGISTER_GAME_OVERRIDE
#define PS2_REGISTER_GAME_OVERRIDE(...)
#include "../../game_hooks/src/kcet_libpad.cpp"

#include <cstdlib>
#include <vector>


int main() {
    std::vector<uint8_t> ram(PS2_RAM_SIZE + 8, 0xA5);
    RdramPadMemory memory;
    memory.Bind(ram.data());
    memory.Store32(PS2_RAM_SIZE - 2, 0);
    if (ram[PS2_RAM_SIZE - 2] != 0xA5 || ram[PS2_RAM_SIZE] != 0xA5) return 1;
    if (memory.Load32(PS2_RAM_SIZE - 2) != 0) return 2;
    memory.Store16(PS2_RAM_SIZE - 1, 0);
    if (ram[PS2_RAM_SIZE - 1] != 0xA5) return 3;
    memory.Store32(0x80000000 + PS2_RAM_SIZE - 4, 0x12345678);
    if (memory.Load32(PS2_RAM_SIZE - 4) != 0x12345678) return 4;
    const uint8_t bytes[4] = {};
    memory.WriteBytes(0xFFFFFFFE, bytes, 4);
    if (ram[0] != 0xA5) return 5;
    uint8_t output[4] = {1, 1, 1, 1};
    memory.ReadBytes(0xFFFFFFFE, output, 4);
    for (uint8_t byte : output) if (byte != 0) return 6;
    memory.Store8(PS2_RAM_SIZE - 1, 0x42);
    if (memory.Load8(PS2_RAM_SIZE - 1) != 0x42) return 7;
    std::vector<uint8_t> scratch(PS2_SCRATCHPAD_SIZE + 8, 0xA5);
    ps2SetScratchpadHostPtr(scratch.data());
    memory.Store32(PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE - 2, 0);
    if (scratch[PS2_SCRATCHPAD_SIZE - 2] != 0xA5) return 9;
    memory.Store16(PS2_SCRATCHPAD_ALIAS_BASE + PS2_SCRATCHPAD_SIZE - 2, 0x1234);
    if (memory.Load16(PS2_SCRATCHPAD_BASE + PS2_SCRATCHPAD_SIZE - 2) != 0x1234) return 10;
    ps2SetScratchpadHostPtr(nullptr);
    if (memory.Load8(PS2_SCRATCHPAD_BASE) != 0) return 11;
    memory.Bind(nullptr);
    if (memory.Load32(0) != 0) return 8;
}
