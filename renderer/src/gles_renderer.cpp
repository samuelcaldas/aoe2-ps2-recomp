#include <aoe2/renderer/gles_renderer.hpp>

#include <iostream>

#if defined(__SWITCH__)
#include <switch.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#elif defined(AOE2_HAS_GLES)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#endif

namespace aoe2::renderer {

GlesRenderer::~GlesRenderer() {
    Shutdown();
}

void GlesRenderer::InitGlesContext() {
#if defined(__SWITCH__)
    // Nintendo Switch EGL initialization via libnx and switch-mesa
    // appletGetOperationMode() can detect handheld (720p) vs docked (1080p)
    gl_context_active_ = true;
#elif defined(AOE2_HAS_GLES)
    // Desktop EGL / GLES3 context initialization
    gl_context_active_ = true;
#else
    // Headless / mock context for CI and tests without display server
    gl_context_active_ = false;
#endif
}

void GlesRenderer::CleanupGlesContext() {
#if defined(__SWITCH__)
    gl_context_active_ = false;
#elif defined(AOE2_HAS_GLES)
    gl_context_active_ = false;
#endif
}

bool GlesRenderer::Initialize(const FramebufferInfo& info) {
    if (initialized_) {
        return true;
    }
    info_ = info;
    InitGlesContext();
    initialized_ = true;
    frame_count_ = 0;
    draw_call_count_ = 0;
    textures_.clear();
    current_frame_commands_.clear();
    return true;
}

void GlesRenderer::BeginFrame() {
    if (!initialized_) return;
    draw_call_count_ = 0;
    current_frame_commands_.clear();

#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (gl_context_active_) {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
#endif
}

void GlesRenderer::EndFrame() {
    if (!initialized_) return;
    ++frame_count_;

#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (gl_context_active_) {
        // eglSwapBuffers(...)
    }
#endif
}

void GlesRenderer::Shutdown() {
    if (!initialized_) return;
    CleanupGlesContext();
    textures_.clear();
    current_frame_commands_.clear();
    initialized_ = false;
}

bool GlesRenderer::UploadTexture(uint32_t texture_id, uint32_t width, uint32_t height, const uint32_t* rgba_pixels) {
    if (!initialized_ || width == 0 || height == 0 || !rgba_pixels) {
        return false;
    }

    GlesTexture tex;
    tex.id = texture_id;
    tex.width = width;
    tex.height = height;
    tex.pixels.assign(rgba_pixels, rgba_pixels + (width * height));

#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (gl_context_active_) {
        GLuint handle = 0;
        glGenTextures(1, &handle);
        glBindTexture(GL_TEXTURE_2D, handle);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_pixels);
        tex.gl_handle = handle;
    }
#endif

    textures_[texture_id] = std::move(tex);
    return true;
}

void GlesRenderer::DrawSprite(uint32_t texture_id, const RectF& src_rect, const RectF& dst_rect, uint32_t tint_color) {
    if (!initialized_) return;
    ++draw_call_count_;
    current_frame_commands_.push_back(DrawSpriteCommand{texture_id, src_rect, dst_rect, tint_color});

#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (gl_context_active_) {
        auto it = textures_.find(texture_id);
        if (it != textures_.end()) {
            glBindTexture(GL_TEXTURE_2D, it->second.gl_handle);
            // Draw textured quad using VBO / shaders
        }
    }
#endif
}

bool GlesRenderer::HasTexture(uint32_t texture_id) const {
    return textures_.find(texture_id) != textures_.end();
}

} // namespace aoe2::renderer
