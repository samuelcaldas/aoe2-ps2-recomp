#include <aoe2/renderer/gles_batch.hpp>

#if defined(__SWITCH__)
#include <GLES2/gl2.h>
#elif defined(AOE2_HAS_GLES)
#include <GLES3/gl3.h>
#endif

namespace aoe2::renderer {

GlesBatch::~GlesBatch() {
    Teardown();
}

void GlesBatch::InitIndices() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    std::array<uint16_t, kMaxIndices> indices{};
    for (uint32_t i = 0; i < kMaxBatchQuads; ++i) {
        uint16_t base = static_cast<uint16_t>(i * 4);
        uint32_t out = i * 6;
        indices[out + 0] = base + 0;
        indices[out + 1] = base + 1;
        indices[out + 2] = base + 2;
        indices[out + 3] = base + 2;
        indices[out + 4] = base + 3;
        indices[out + 5] = base + 0;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices.data(), GL_STATIC_DRAW);
#endif
}

bool GlesBatch::Initialize() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    if (!vbo_ || !ebo_) { Teardown(); return false; }
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_buffer_), nullptr, GL_DYNAMIC_DRAW);
    InitIndices();
    if (glGetError() != GL_NO_ERROR) { Teardown(); return false; }
    return true;
#endif
    return false;
}

void GlesBatch::Begin() {
    vertex_count_ = 0;
    current_texture_ = 0;
    flushes_ = 0;
}

void GlesBatch::SetupAttributes() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<void*>(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex2D), reinterpret_cast<void*>(8));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex2D), reinterpret_cast<void*>(16));
#endif
}

void GlesBatch::Flush() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (vertex_count_ == 0) return;
    glBindTexture(GL_TEXTURE_2D, current_texture_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferSubData(GL_ARRAY_BUFFER, 0, vertex_count_ * sizeof(Vertex2D), vertex_buffer_.data());
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    SetupAttributes();
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>((vertex_count_ / 4) * 6), GL_UNSIGNED_SHORT, nullptr);
    ++flushes_;
    vertex_count_ = 0;
#endif
}

void GlesBatch::PushQuad(const Vertex2D vertices[4], uint32_t gl_texture) {
    if (gl_texture != current_texture_ || vertex_count_ + 4 > kMaxVertices) Flush();
    current_texture_ = gl_texture;
    for (int i = 0; i < 4; ++i) vertex_buffer_[vertex_count_++] = vertices[i];
}

void GlesBatch::Teardown() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
#endif
    vbo_ = 0; ebo_ = 0; vertex_count_ = 0; current_texture_ = 0;
}

} // namespace aoe2::renderer
