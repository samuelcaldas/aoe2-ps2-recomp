#pragma once

#include <cstdint>

namespace aoe2::renderer {

struct Vertex2D {
    float x{0.0f};
    float y{0.0f};
    float u{0.0f};
    float v{0.0f};
    uint32_t color{0xFFFFFFFF};
};

inline constexpr uint32_t kMaxBatchQuads = 2048;
inline constexpr uint32_t kMaxVertices = kMaxBatchQuads * 4;
inline constexpr uint32_t kMaxIndices = kMaxBatchQuads * 6;
inline constexpr uint32_t kFontTextureId = 0xFFFFFFFE;

} // namespace aoe2::renderer
