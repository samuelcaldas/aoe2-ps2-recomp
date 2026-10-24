// Pure part of the module state snapshot observer (AOE2_STATE_SNAPSHOT=1): regions, schedule and text format.
// Reading guest memory lives in state_snapshot_capture.hpp so this header builds without runtime headers.
//
// Snapshot n of a module is logged for n = 1..16 and then every 64th, at the return of the module's per-frame
// update, identically for the original (call trace) and the native handler.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>

namespace aoe2::game_hooks::diagnostics {

struct StateRegion {
    const char* label;
    uint32_t address;
    uint32_t size;
};

// Work areas of the modules, from the externs of the decomp sources (see docs/sot/input-modules-semantics.md).
inline constexpr StateRegion kMouseRegion{"mouse", 0x00509F00, 0x3C0};
inline constexpr StateRegion kKeyboardRegion{"keyboard", 0x0050A2C0, 0x280};

inline bool Enabled() {
    static const bool enabled = std::getenv("AOE2_STATE_SNAPSHOT") != nullptr;
    return enabled;
}

inline bool ShouldSnapshot(uint64_t n) { return n >= 1 && (n <= 16 || n % 64 == 0); }

inline uint64_t Fnv1a(const uint8_t* bytes, std::size_t size) {
    uint64_t hash = 14695981039346656037ull;
    for (std::size_t i = 0; i < size; ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
    return hash;
}

inline std::string FormatConfig(bool enabled) {
    return std::string("[state-snapshot-config] enabled=") + (enabled ? "1" : "0");
}

inline std::string FormatSkip(const StateRegion& region, uint64_t n, const char* reason) {
    std::ostringstream out;
    out << "[state-snapshot-skip] label=" << region.label << " n=" << n << " reason=" << reason;
    return out.str();
}

inline std::string FormatSnapshot(const StateRegion& region, uint64_t n, uint64_t input_steps, const uint8_t* bytes) {
    std::ostringstream out;
    out << "[state-snapshot] label=" << region.label << " n=" << n << " steps=" << input_steps << " fnv=" << std::hex
        << Fnv1a(bytes, region.size) << " bytes=";
    for (uint32_t i = 0; i < region.size; ++i) out << std::setw(2) << std::setfill('0') << unsigned(bytes[i]);
    return out.str();
}

}  // namespace aoe2::game_hooks::diagnostics
