#include <aoe2/renderer/gles_renderer.hpp>

#include <iostream>

#if defined(__SWITCH__)
#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#elif defined(AOE2_HAS_GLES)
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#endif

namespace aoe2::renderer {

#if defined(__SWITCH__)
namespace {
static EGLDisplay s_switch_display = EGL_NO_DISPLAY;
static EGLContext s_switch_context = EGL_NO_CONTEXT;
static EGLSurface s_switch_surface = EGL_NO_SURFACE;
}
#endif

GlesRenderer::~GlesRenderer() {
    Shutdown();
}

void GlesRenderer::InitGlesContext() {
#if defined(__SWITCH__)
    NWindow* win = nwindowGetDefault();
    if (win) {
        AppletOperationMode op_mode = appletGetOperationMode();
        u32 winWidth = (op_mode == AppletOperationMode_Console) ? 1920 : 1280;
        u32 winHeight = (op_mode == AppletOperationMode_Console) ? 1080 : 720;
        nwindowSetDimensions(win, winWidth, winHeight);

        s_switch_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s_switch_display && eglInitialize(s_switch_display, nullptr, nullptr)) {
            eglBindAPI(EGL_OPENGL_ES_API);
            static const EGLint configAttribs[] = {
                EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_NONE
            };
            EGLConfig config;
            EGLint numConfigs = 0;
            if (eglChooseConfig(s_switch_display, configAttribs, &config, 1, &numConfigs) && numConfigs > 0) {
                s_switch_surface = eglCreateWindowSurface(s_switch_display, config, win, nullptr);
                static const EGLint ctxAttribs[] = {
                    EGL_CONTEXT_CLIENT_VERSION, 2,
                    EGL_NONE
                };
                s_switch_context = eglCreateContext(s_switch_display, config, EGL_NO_CONTEXT, ctxAttribs);
                if (s_switch_context && s_switch_surface) {
                    eglMakeCurrent(s_switch_display, s_switch_surface, s_switch_surface, s_switch_context);
                    eglSwapInterval(s_switch_display, 1);
                    gl_context_active_ = true;
                    return;
                }
            }
        }
    }
    gl_context_active_ = false;
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
    if (s_switch_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(s_switch_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (s_switch_context != EGL_NO_CONTEXT) {
            eglDestroyContext(s_switch_display, s_switch_context);
            s_switch_context = EGL_NO_CONTEXT;
        }
        if (s_switch_surface != EGL_NO_SURFACE) {
            eglDestroySurface(s_switch_display, s_switch_surface);
            s_switch_surface = EGL_NO_SURFACE;
        }
        eglTerminate(s_switch_display);
        s_switch_display = EGL_NO_DISPLAY;
    }
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
    current_text_commands_.clear();
    return true;
}

void GlesRenderer::BeginFrame() {
    if (!initialized_) return;
    draw_call_count_ = 0;
    current_frame_commands_.clear();
    current_text_commands_.clear();

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

#if defined(__SWITCH__)
    if (gl_context_active_ && s_switch_display != EGL_NO_DISPLAY && s_switch_surface != EGL_NO_SURFACE) {
        eglSwapBuffers(s_switch_display, s_switch_surface);
    }
#elif defined(AOE2_HAS_GLES)
    if (gl_context_active_) {
        // eglSwapBuffers(...)
    }
#endif
}

void GlesRenderer::Shutdown() {
    if (!initialized_) return;
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (gl_context_active_) {
        for (auto& [id, tex] : textures_) {
            if (tex.gl_handle != 0) {
                glDeleteTextures(1, &tex.gl_handle);
                tex.gl_handle = 0;
            }
        }
    }
#endif
    CleanupGlesContext();
    textures_.clear();
    current_frame_commands_.clear();
    current_text_commands_.clear();
    initialized_ = false;
}

void GlesRenderer::DeleteTexture(uint32_t texture_id) {
    auto it = textures_.find(texture_id);
    if (it == textures_.end()) return;

#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    if (gl_context_active_ && it->second.gl_handle != 0) {
        glDeleteTextures(1, &it->second.gl_handle);
    }
#endif
    textures_.erase(it);
}

bool GlesRenderer::UploadTexture(uint32_t texture_id, uint32_t width, uint32_t height, const uint32_t* rgba_pixels) {
    if (!initialized_ || width == 0 || height == 0 || !rgba_pixels) {
        return false;
    }

    // Clean up previous GL handle if replacing existing texture
    auto it = textures_.find(texture_id);
    if (it != textures_.end()) {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
        if (gl_context_active_ && it->second.gl_handle != 0) {
            glDeleteTextures(1, &it->second.gl_handle);
            it->second.gl_handle = 0;
        }
#endif
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
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba_pixels);
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

void GlesRenderer::DrawText(std::string_view text, float x, float y, float font_size, uint32_t color) {
    if (!initialized_) return;
    current_text_commands_.push_back(DrawTextCommand{std::string(text), x, y, font_size, color});
}

bool GlesRenderer::HasTexture(uint32_t texture_id) const {
    return textures_.find(texture_id) != textures_.end();
}

const GlesTexture* GlesRenderer::GetTexture(uint32_t texture_id) const {
    auto it = textures_.find(texture_id);
    if (it != textures_.end()) {
        return &it->second;
    }
    return nullptr;
}

} // namespace aoe2::renderer
