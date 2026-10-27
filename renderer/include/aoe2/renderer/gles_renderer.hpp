#pragma once

#include <aoe2/renderer/renderer.hpp>
#include <cstdint>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace aoe2::renderer {

struct GlesTexture {
    uint32_t id{0};
    uint32_t width{0};
    uint32_t height{0};
    uint32_t gl_handle{0};
    std::vector<uint32_t> pixels;
};

struct DrawSpriteCommand {
    uint32_t texture_id{0};
    RectF src_rect{};
    RectF dst_rect{};
    uint32_t tint_color{0xFFFFFFFF};
};

class GlesRenderer : public IRenderer {
public:
    GlesRenderer() = default;
    ~GlesRenderer() override;

    bool Initialize(const FramebufferInfo& info) override;
    void BeginFrame() override;
    void EndFrame() override;
    void Shutdown() override;
    std::string_view GetBackendName() const override { return "GlesRenderer"; }
    uint64_t GetFrameCount() const override { return frame_count_; }

    bool UploadTexture(uint32_t texture_id, uint32_t width, uint32_t height, const uint32_t* rgba_pixels) override;
    void DrawSprite(uint32_t texture_id, const RectF& src_rect, const RectF& dst_rect, uint32_t tint_color = 0xFFFFFFFF) override;
    bool HasTexture(uint32_t texture_id) const override;
    void DeleteTexture(uint32_t texture_id) override;

    uint32_t GetDrawCallCount() const { return draw_call_count_; }
    bool IsInitialized() const { return initialized_; }
    const FramebufferInfo& GetFramebufferInfo() const { return info_; }

private:
    void InitGlesContext();
    void CleanupGlesContext();

    FramebufferInfo info_{};
    uint64_t frame_count_{0};
    uint32_t draw_call_count_{0};
    bool initialized_{false};
    bool gl_context_active_{false};

    std::unordered_map<uint32_t, GlesTexture> textures_;
    std::vector<DrawSpriteCommand> current_frame_commands_;
};

} // namespace aoe2::renderer
