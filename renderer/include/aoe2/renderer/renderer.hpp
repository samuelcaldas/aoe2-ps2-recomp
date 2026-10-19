#pragma once

#include <cstdint>
#include <string_view>

namespace aoe2::renderer {

struct FramebufferInfo {
    uint32_t width{640};
    uint32_t height{480};
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
