#pragma once

#include <aoe2/renderer/gles_types.hpp>
#include <array>
#include <cstdint>

namespace aoe2::renderer {

class GlesBatch {
public:
    GlesBatch() = default;
    ~GlesBatch();

    bool Initialize();
    void Begin();
    void PushQuad(const Vertex2D vertices[4], uint32_t gl_texture);
    void Flush();
    void Teardown();

    uint32_t GetFlushes() const { return flushes_; }

private:
    void InitIndices();
    void SetupAttributes();

    uint32_t vao_{0};
    uint32_t vbo_{0};
    uint32_t ebo_{0};
    uint32_t current_texture_{0};
    uint32_t vertex_count_{0};
    uint32_t flushes_{0};
    std::array<Vertex2D, kMaxVertices> vertex_buffer_{};
};

} // namespace aoe2::renderer
