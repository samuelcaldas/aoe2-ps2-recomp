#pragma once

#include "state_snapshot.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace aoe2::game_hooks::diagnostics {

// Work area from decomp D_38C730 (kt_gpu command dispatch table and parameters).
inline constexpr StateRegion kGpuRegion{"gpu", 0x0038C730, 0x080};

inline const uint8_t* ResolveGpuRdramOffset(const uint8_t* rdram, uint32_t address, uint32_t size) {
    if (!rdram) return nullptr;
    const uint32_t offset = address & 0x01FFFFFFu;
    if (offset + size > 32 * 1024 * 1024) return nullptr;
    return rdram + offset;
}

inline std::string FormatGpuSnapshot(const uint8_t* rdram, uint64_t n, uint64_t steps) {
    const uint8_t* ptr = ResolveGpuRdramOffset(rdram, kGpuRegion.address, kGpuRegion.size);
    if (!ptr) {
        return FormatSkip(kGpuRegion, n, "invalid-range");
    }
    return FormatSnapshot(kGpuRegion, n, steps, ptr);
}

inline void ObserveGpuState(const uint8_t* rdram, uint64_t step) {
    if (!Enabled()) return;
    static uint64_t count = 0;
    const uint64_t n = ++count;
    if (!ShouldSnapshot(n)) return;
    std::cerr << FormatGpuSnapshot(rdram, n, step) << std::endl;
}

}  // namespace aoe2::game_hooks::diagnostics
