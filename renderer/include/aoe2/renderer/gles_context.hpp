#pragma once

#include <cstdint>

namespace aoe2::renderer {

class GlesContext {
public:
    GlesContext() = default;
    ~GlesContext();

    bool Initialize(uint32_t width, uint32_t height);
    void SwapBuffers();
    void Teardown();

    bool IsActive() const { return active_; }
    uint32_t GetWidth() const { return width_; }
    uint32_t GetHeight() const { return height_; }

private:
    bool InitSwitchEgl(uint32_t width, uint32_t height);
    bool InitDesktopEgl(uint32_t width, uint32_t height);

    uint32_t width_{0};
    uint32_t height_{0};
    bool active_{false};
    void* display_{nullptr};
    void* surface_{nullptr};
    void* context_{nullptr};
};

} // namespace aoe2::renderer
