#pragma once

#include "state_snapshot.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace aoe2::game_hooks::diagnostics {

// Work area from decomp D_4E2CB8 (kt_mc_2 work area).
inline constexpr StateRegion kMcRegion{"mc", 0x004E2CB8, 0x100};

inline const uint8_t* ResolveMcRdramOffset(const uint8_t* rdram, uint32_t address, uint32_t size) {
    if (!rdram) return nullptr;
    const uint32_t offset = address & 0x03FFFFFFu;
    if (offset + size > 64 * 1024 * 1024) return nullptr;
    return rdram + offset;
}

inline std::string FormatMcSnapshot(const uint8_t* rdram, uint64_t n, uint64_t steps) {
    const uint8_t* ptr = ResolveMcRdramOffset(rdram, kMcRegion.address, kMcRegion.size);
    if (!ptr) {
        return FormatSkip(kMcRegion, n, "invalid-range");
    }
    return FormatSnapshot(kMcRegion, n, steps, ptr);
}

inline void ObserveMcState(const uint8_t* rdram, uint64_t step) {
    if (!Enabled()) return;
    static uint64_t count = 0;
    const uint64_t n = ++count;
    if (!ShouldSnapshot(n)) return;
    std::cerr << FormatMcSnapshot(rdram, n, step) << std::endl;
}

}  // namespace aoe2::game_hooks::diagnostics
