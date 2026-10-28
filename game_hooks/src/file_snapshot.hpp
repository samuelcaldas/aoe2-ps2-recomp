#pragma once

#include "state_snapshot.hpp"

#include <cstdint>
#include <iostream>
#include <string>

namespace aoe2::game_hooks::diagnostics {

// Work areas from decomp D_4F1508 (CdServerState, 20 bytes) and D_4F16C0 (CdStreamStatus, 12 bytes).
inline constexpr StateRegion kFileRegion{"file", 0x004F1508, 0x1B8};
inline constexpr StateRegion kCdvdRegion{"cdvd", 0x004F16C0, 0x020};

inline const uint8_t* ResolveRdramOffset(const uint8_t* rdram, uint32_t address, uint32_t size) {
    if (!rdram) return nullptr;
    const uint32_t offset = address & 0x03FFFFFFu;
    if (offset + size > 64 * 1024 * 1024) return nullptr;
    return rdram + offset;
}

inline std::string FormatFileSnapshot(const uint8_t* rdram, uint64_t n, uint64_t steps) {
    const uint8_t* ptr = ResolveRdramOffset(rdram, kFileRegion.address, kFileRegion.size);
    if (!ptr) {
        return FormatSkip(kFileRegion, n, "invalid-range");
    }
    return FormatSnapshot(kFileRegion, n, steps, ptr);
}

inline std::string FormatCdvdSnapshot(const uint8_t* rdram, uint64_t n, uint64_t steps) {
    const uint8_t* ptr = ResolveRdramOffset(rdram, kCdvdRegion.address, kCdvdRegion.size);
    if (!ptr) {
        return FormatSkip(kCdvdRegion, n, "invalid-range");
    }
    return FormatSnapshot(kCdvdRegion, n, steps, ptr);
}

inline void ObserveFileState(const uint8_t* rdram, uint64_t step) {
    if (!Enabled()) return;
    static uint64_t count = 0;
    const uint64_t n = ++count;
    if (!ShouldSnapshot(n)) return;
    std::cerr << FormatFileSnapshot(rdram, n, step) << std::endl;
}

inline void ObserveCdvdState(const uint8_t* rdram, uint64_t step) {
    if (!Enabled()) return;
    static uint64_t count = 0;
    const uint64_t n = ++count;
    if (!ShouldSnapshot(n)) return;
    std::cerr << FormatCdvdSnapshot(rdram, n, step) << std::endl;
}

}  // namespace aoe2::game_hooks::diagnostics
