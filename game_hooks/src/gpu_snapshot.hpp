#pragma once
#include "state_snapshot.hpp"
#include <cstring>
#include <iostream>
#include <vector>

namespace aoe2::game_hooks::diagnostics {
inline const uint8_t* ResolveGpuRdramOffset(const uint8_t* ram, uint32_t address, uint32_t size) {
    const auto offset = address & 0x1ffffffu;
    if (!ram || size > 0x2000000u - offset) return nullptr;
    return ram + offset;
}
// Entry snapshot: the 16-byte writer context followed by its current packet.
// Empty writers capture context only. Oversized/invalid ranges are explicit skips.
inline std::string FormatGpuSnapshot(const uint8_t* ram, uint32_t context,
                                     uint32_t entry, uint64_t n) {
    const StateRegion region{"gpu", context, 16};
    const auto ptr = ResolveGpuRdramOffset(ram, context, 16);
    if (!ptr) return FormatSkip(region, n, "invalid-context");
    uint32_t cursor, begin, tag;
    std::memcpy(&cursor, ptr, 4);
    std::memcpy(&begin, ptr + 8, 4);
    std::memcpy(&tag, ptr + 12, 4);
    if (!begin) begin = tag;
    const uint32_t size = begin ? cursor - begin : 0;
    if (size > 65536) return FormatSkip(region, n, "packet-too-large");
    const auto packet = ResolveGpuRdramOffset(ram, begin, size);
    if (!packet) return FormatSkip(region, n, "invalid-packet");
    std::vector<uint8_t> bytes(ptr, ptr + 16);
    bytes.insert(bytes.end(), packet, packet + size);
    const StateRegion captured{"gpu", context, uint32_t(bytes.size())};
    return FormatSnapshot(captured, n, entry, bytes.data());
}
inline uint64_t gpu_snapshot_count = 0;
inline void ObserveGpuState(const uint8_t* ram, uint32_t context, uint32_t entry) {
    if (!Enabled()) return;
    const auto n = ++gpu_snapshot_count;
    if (ShouldSnapshot(n)) std::cerr << FormatGpuSnapshot(ram, context, entry, n) << '\n';
}
}
