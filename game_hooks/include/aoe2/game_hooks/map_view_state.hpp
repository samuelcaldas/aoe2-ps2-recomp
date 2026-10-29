#pragma once
#include <cstdint>

namespace aoe2::game_hooks {
struct IndexedMapSurfaceLayout {
    uint32_t pitch;
    uint32_t visible_offset;
    uint32_t buffer_stride;
    uint32_t total_bytes;

    /// Size two indexed buffers, five guard rows per edge and external canaries; throws on invalid bounds.
    static IndexedMapSurfaceLayout Calculate(uint64_t width, uint64_t height, uint64_t capacity);
};
}
