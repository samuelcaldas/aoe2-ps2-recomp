#include <aoe2/renderer/gles_context.hpp>

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

GlesContext::~GlesContext() {
    Teardown();
}

bool GlesContext::Initialize(uint32_t width, uint32_t height) {
    if (active_) return true;
    if (width == 0 || height == 0) return false;
    width_ = width;
    height_ = height;
#if defined(__SWITCH__)
    return InitSwitchEgl(width, height);
#elif defined(AOE2_HAS_GLES)
    return InitDesktopEgl(width, height);
#endif
    return false;
}

#if defined(__SWITCH__)
bool GlesContext::InitSwitchEgl(uint32_t width, uint32_t height) {
    NWindow* win = nwindowGetDefault();
    if (!win) return false;
    nwindowSetDimensions(win, width, height);
    EGLDisplay disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (disp == EGL_NO_DISPLAY || !eglInitialize(disp, nullptr, nullptr)) return false;
    display_ = disp;
    if (!eglBindAPI(EGL_OPENGL_ES_API)) { Teardown(); return false; }
    static const EGLint cfgAttrs[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg; EGLint num = 0;
    if (!eglChooseConfig(disp, cfgAttrs, &cfg, 1, &num) || num <= 0) { Teardown(); return false; }
    EGLSurface surf = eglCreateWindowSurface(disp, cfg, win, nullptr);
    surface_ = surf;
    static const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(disp, cfg, EGL_NO_CONTEXT, ctxAttrs);
    context_ = ctx;
    if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT || !eglMakeCurrent(disp, surf, surf, ctx)) {
        Teardown(); return false;
    }
    eglSwapInterval(disp, 1);
    display_ = disp; surface_ = surf; context_ = ctx; active_ = true;
    return true;
}
#else
bool GlesContext::InitSwitchEgl(uint32_t, uint32_t) {
    return false;
}
#endif

#if defined(AOE2_HAS_GLES)
bool GlesContext::InitDesktopEgl(uint32_t width, uint32_t height) {
    EGLDisplay disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (disp == EGL_NO_DISPLAY || !eglInitialize(disp, nullptr, nullptr)) return false;
    display_ = disp;
    if (!eglBindAPI(EGL_OPENGL_ES_API)) { Teardown(); return false; }
    static const EGLint cfgAttrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE
    };
    EGLConfig cfg; EGLint num = 0;
    if (!eglChooseConfig(disp, cfgAttrs, &cfg, 1, &num) || num <= 0) { Teardown(); return false; }
    const EGLint pbAttrs[] = {
        EGL_WIDTH, static_cast<EGLint>(width),
        EGL_HEIGHT, static_cast<EGLint>(height),
        EGL_NONE
    };
    EGLSurface surf = eglCreatePbufferSurface(disp, cfg, pbAttrs);
    surface_ = surf;
    static const EGLint ctxAttrs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    EGLContext ctx = eglCreateContext(disp, cfg, EGL_NO_CONTEXT, ctxAttrs);
    context_ = ctx;
    if (surf == EGL_NO_SURFACE || ctx == EGL_NO_CONTEXT || !eglMakeCurrent(disp, surf, surf, ctx)) {
        Teardown(); return false;
    }
    display_ = disp; surface_ = surf; context_ = ctx; active_ = true;
    return true;
}
#else
bool GlesContext::InitDesktopEgl(uint32_t, uint32_t) {
    return false;
}
#endif

void GlesContext::SwapBuffers() {
    if (!active_ || !display_ || !surface_) return;
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    eglSwapBuffers(static_cast<EGLDisplay>(display_), static_cast<EGLSurface>(surface_));
#endif
}

void GlesContext::Teardown() {
#if defined(AOE2_HAS_GLES) || defined(__SWITCH__)
    auto disp = static_cast<EGLDisplay>(display_);
    auto ctx = static_cast<EGLContext>(context_);
    auto surf = static_cast<EGLSurface>(surface_);
    if (disp != EGL_NO_DISPLAY) {
        eglMakeCurrent(disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (ctx != EGL_NO_CONTEXT) eglDestroyContext(disp, ctx);
        if (surf != EGL_NO_SURFACE) eglDestroySurface(disp, surf);
        eglTerminate(disp);
    }
#endif
    active_ = false; display_ = nullptr; surface_ = nullptr; context_ = nullptr;
    width_ = 0; height_ = 0;
}

} // namespace aoe2::renderer
