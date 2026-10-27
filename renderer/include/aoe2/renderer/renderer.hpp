#pragma once

#include <cstdint>
#include <string_view>

namespace aoe2::renderer {

struct FramebufferInfo {
    uint32_t width{640};
    uint32_t height{480};
};

struct RectF {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};
};

class IRenderer {
public:
    virtual ~IRenderer() = default;

    virtual bool Initialize(const FramebufferInfo& info) = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Shutdown() = 0;
    virtual std::string_view GetBackendName() const = 0;
    virtual uint64_t GetFrameCount() const = 0;

    virtual bool UploadTexture(uint32_t texture_id, uint32_t width, uint32_t height, const uint32_t* rgba_pixels) {
        return false;
    }
    virtual void DrawSprite(uint32_t texture_id, const RectF& src_rect, const RectF& dst_rect, uint32_t tint_color = 0xFFFFFFFF) {}
    virtual bool HasTexture(uint32_t texture_id) const { return false; }
};

class NullRenderer : public IRenderer {
public:
    NullRenderer() = default;
    ~NullRenderer() override = default;

    bool Initialize(const FramebufferInfo& info) override;
    void BeginFrame() override;
    void EndFrame() override;
    void Shutdown() override;
    std::string_view GetBackendName() const override;
    uint64_t GetFrameCount() const override;

private:
    FramebufferInfo info_{};
    uint64_t frame_count_{0};
    bool initialized_{false};
};

} // namespace aoe2::renderer
