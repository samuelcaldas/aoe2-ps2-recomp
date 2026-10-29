#include <aoe2/renderer/gles_renderer.hpp>

#if defined(__SWITCH__)
#include <GLES2/gl2.h>
#elif defined(AOE2_HAS_GLES)
#include <GLES3/gl3.h>
#endif

namespace aoe2::renderer {

GlesRenderer::~GlesRenderer() {
    Shutdown();
}

bool GlesRenderer::InitGlObjects() {
    if (!context_.IsActive() || !shader_.Initialize() || !batch_.Initialize()) return false;
    return font_.Initialize(*this);
}

bool GlesRenderer::Initialize(const FramebufferInfo& info) {
    if (info.width == 0 || info.height == 0) return false;
    if (initialized_) {
        if (info_.width == info.width && info_.height == info.height) return true;
        Shutdown();
    }
    info_ = info;
    // UploadTexture is used by GlesFont::Initialize, so mark the renderer
    // initialized while constructing GL resources. Roll back all partial state
    // if the EGL context or any dependent object fails to initialize.
    initialized_ = true;
    if (!context_.Initialize(info.width, info.height) || !InitGlObjects()) {
        Shutdown();
        return false;
    }
    frame_count_ = 0;
    draw_call_count_ = 0;
    return true;
}

void GlesRenderer::BeginFrame() {
    if (!initialized_) return;
    draw_call_count_ = 0;
    current_frame_commands_.clear();
    current_text_commands_.clear();
    if (!context_.IsActive()) return;
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    glViewport(0, 0, static_cast<GLsizei>(info_.width), static_cast<GLsizei>(info_.height));
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
#endif
    shader_.Bind();
    shader_.UpdateProjection(static_cast<float>(info_.width), static_cast<float>(info_.height));
    batch_.Begin();
}

void GlesRenderer::EndFrame() {
    if (!initialized_) return;
    ++frame_count_;
    if (!context_.IsActive()) return;
    batch_.Flush();
    context_.SwapBuffers();
}

void GlesRenderer::Shutdown() {
    if (!initialized_) return;
    for (auto& [id, tex] : textures_) {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
        if (context_.IsActive() && tex.gl_handle != 0) glDeleteTextures(1, &tex.gl_handle);
#endif
    }
    batch_.Teardown();
    shader_.Teardown();
    font_.Teardown();
    context_.Teardown();
    textures_.clear();
    current_frame_commands_.clear();
    current_text_commands_.clear();
    initialized_ = false;
}

void GlesRenderer::DeleteTexture(uint32_t texture_id) {
    auto it = textures_.find(texture_id);
    if (it == textures_.end()) return;
    if (context_.IsActive()) batch_.Flush();
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (context_.IsActive() && it->second.gl_handle != 0) glDeleteTextures(1, &it->second.gl_handle);
#endif
    textures_.erase(it);
}

bool GlesRenderer::UploadTexture(uint32_t texture_id, uint32_t width, uint32_t height, const uint32_t* rgba_pixels) {
    if (!initialized_ || width == 0 || height == 0 || !rgba_pixels) return false;
    GlesTexture tex{texture_id, width, height, 0, {}};
    const size_t pixel_count = static_cast<size_t>(width) * height;
    if (pixel_count / width != height || pixel_count > tex.pixels.max_size()) return false;
    tex.pixels.assign(rgba_pixels, rgba_pixels + pixel_count);
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (context_.IsActive()) {
        GLuint handle = 0;
        glGenTextures(1, &handle);
        if (!handle) return false;
        glBindTexture(GL_TEXTURE_2D, handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_pixels);
        if (glGetError() != GL_NO_ERROR) { glDeleteTextures(1, &handle); return false; }
        tex.gl_handle = handle;
    }
#endif
    DeleteTexture(texture_id);
    textures_[texture_id] = std::move(tex);
    return true;
}

void GlesRenderer::BuildSpriteVertices(Vertex2D v[4], const RectF& src, const RectF& dst,
                                      float tw, float th, uint32_t tint) {
    float u0 = src.x / tw, v0 = src.y / th;
    float u1 = (src.x + src.width) / tw, v1 = (src.y + src.height) / th;
    float x0 = dst.x, y0 = dst.y;
    float x1 = dst.x + dst.width, y1 = dst.y + dst.height;
    v[0] = {x0, y0, u0, v0, tint};
    v[1] = {x1, y0, u1, v0, tint};
    v[2] = {x1, y1, u1, v1, tint};
    v[3] = {x0, y1, u0, v1, tint};
}

void GlesRenderer::DrawSprite(uint32_t texture_id, const RectF& src_rect, const RectF& dst_rect, uint32_t tint_color) {
    if (!initialized_) return;
    ++draw_call_count_;
    current_frame_commands_.push_back(DrawSpriteCommand{texture_id, src_rect, dst_rect, tint_color});
    if (!context_.IsActive()) return;
    auto it = textures_.find(texture_id);
    if (it == textures_.end() || it->second.gl_handle == 0) return;
    Vertex2D v[4];
    BuildSpriteVertices(v, src_rect, dst_rect, static_cast<float>(it->second.width),
                        static_cast<float>(it->second.height), tint_color);
    batch_.PushQuad(v, it->second.gl_handle);
}

void GlesRenderer::DrawText(std::string_view text, float x, float y, float font_size, uint32_t color) {
    if (!initialized_) return;
    current_text_commands_.push_back(DrawTextCommand{std::string(text), x, y, font_size, color});
    font_.DrawString(*this, text, x, y, font_size, color);
}

bool GlesRenderer::HasTexture(uint32_t texture_id) const {
    return textures_.find(texture_id) != textures_.end();
}

const GlesTexture* GlesRenderer::GetTexture(uint32_t texture_id) const {
    auto it = textures_.find(texture_id);
    if (it != textures_.end()) return &it->second;
    return nullptr;
}

} // namespace aoe2::renderer
