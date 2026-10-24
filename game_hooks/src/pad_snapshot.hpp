// Shared diagnostics at return from the outer per-slot kt_libpad step, for both implementations.
#pragma once
#include "guest_rdram_access.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <unordered_map>

namespace aoe2::game_hooks::diagnostics {
inline std::array<uint64_t, 2> input_samples{};
// Logical input advances once per outer slot step, independently of State/Read/rumble queries.
inline std::array<uint64_t, 2> logical_steps{};
inline bool SyntheticCrossDown(uint64_t fallback_sample) {
    const uint64_t sample = logical_steps[0] ? logical_steps[0] - 1 : fallback_sample;
    return (sample / 100) % 2 == 0;
}
inline void BeginPadStep(uint8_t* memory, uint32_t address) {
    if (!std::getenv("AOE2_PAD_SNAPSHOT")) return;
    uint32_t offset = 0;
    bool scratch = false;
    if (!memory || !resolveEeGuestRange(0x004EA148, 8, offset, scratch) || scratch) return;
    uint32_t slots = 0, count = 0;
    std::memcpy(&slots, memory + offset, 4);
    std::memcpy(&count, memory + offset + 4, 4);
    if (!slots || address < slots || (address - slots) % 0x200 != 0) return;
    const uint32_t index = (address - slots) / 0x200;
    if (index < count && index < logical_steps.size()) ++logical_steps[index];
}
inline bool native_libpad_installed = false;
inline void SnapshotSkip(const char* reason) {
    static uint64_t skipped = 0;
    if (++skipped <= 3 || (skipped & (skipped - 1)) == 0)
        std::cerr << "[pad-snapshot-skip] reason=" << reason << " n=" << skipped << '\n';
}
inline void LogSlotSnapshot(uint8_t* memory, uint32_t address, int index, uint64_t update) {
    uint32_t offset = 0;
    bool scratch = false;
    if (!memory || !resolveEeGuestRange(address, 0x200, offset, scratch)) {
        SnapshotSkip("invalid-slot-range");
        return;
    }
    const uint8_t* base = scratch ? ps2GetScratchpadHostPtr() : memory;
    if (!base) { SnapshotSkip("missing-backing-memory"); return; }
    const uint8_t* slot = base + offset;
    uint64_t hash = 14695981039346656037ull;
    for (int i = 0; i < 0x200; ++i) hash = (hash ^ slot[i]) * 1099511628211ull;
    std::cerr << "[pad-snapshot] n=" << update << " slot=" << index
              << " samples0=" << input_samples[0] << " samples1=" << input_samples[1]
              << " fnv=" << std::hex << hash << " bytes=";
    for (int i = 0; i < 0x200; ++i)
        std::cerr << std::setw(2) << std::setfill('0') << unsigned(slot[i]);
    std::cerr << std::setfill(' ') << std::dec << '\n';
}
inline void LogPadStep(uint8_t* memory, uint32_t address) {
    static bool announced = false;
    const bool enabled = std::getenv("AOE2_PAD_SNAPSHOT") != nullptr;
    if (!announced) {
        std::cerr << "[pad-snapshot-config] observer=StepSlot enabled=" << enabled << '\n';
        announced = true;
    }
    if (!enabled) return;
    static std::unordered_map<uint32_t, uint64_t> updates;
    const uint64_t n = ++updates[address];
    if (n > 16 && n % 64 != 0) return;
    uint32_t offset = 0;
    bool scratch = false;
    if (!memory || !resolveEeGuestRange(0x004EA148, 8, offset, scratch) || scratch) {
        SnapshotSkip("invalid-work-range");
        return;
    }
    uint32_t slots = 0, count = 0;
    std::memcpy(&slots, memory + offset, 4);
    std::memcpy(&count, memory + offset + 4, 4);
    if (!slots || address < slots || (address - slots) % 0x200 != 0 ||
        (address - slots) / 0x200 >= count) {
        SnapshotSkip("slot-outside-work-array");
        return;
    }
    LogSlotSnapshot(memory, address, int((address - slots) / 0x200), n);
}
} // namespace aoe2::game_hooks::diagnostics
