#pragma once

#include "state_snapshot.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace aoe2::game_hooks::diagnostics {

// Work area from decomp D_4EA180 (kt_hd work area).
inline constexpr StateRegion kHdRegion{"hd", 0x004EA180, 0x080};

inline const uint8_t* ResolveHdRdramOffset(const uint8_t* rdram, uint32_t address, uint32_t size) {
    if (!rdram) return nullptr;
    const uint32_t offset = address & 0x03FFFFFFu;
    if (size > 64 * 1024 * 1024 - offset) return nullptr;
    return rdram + offset;
}

inline std::string FormatHdSnapshot(const uint8_t* rdram, uint64_t n, uint64_t steps) {
    const uint8_t* ptr = ResolveHdRdramOffset(rdram, kHdRegion.address, kHdRegion.size);
    if (!ptr) {
        return FormatSkip(kHdRegion, n, "invalid-range");
    }
    return FormatSnapshot(kHdRegion, n, steps, ptr);
}

inline void ObserveHdState(const uint8_t* rdram, uint64_t step) {
    if (!Enabled()) return;
    static uint64_t count = 0;
    const uint64_t n = ++count;
    if (!ShouldSnapshot(n)) return;
    std::cerr << FormatHdSnapshot(rdram, n, step) << std::endl;
}

}  // namespace aoe2::game_hooks::diagnostics
