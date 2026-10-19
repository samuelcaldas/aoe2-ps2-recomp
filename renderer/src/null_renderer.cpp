#include <aoe2/renderer/renderer.hpp>

namespace aoe2::renderer {

bool NullRenderer::Initialize(const FramebufferInfo& info) {
    info_ = info;
    initialized_ = true;
    frame_count_ = 0;
    return true;
}

void NullRenderer::BeginFrame() {
    // Null renderer: no-op
}

void NullRenderer::EndFrame() {
    if (initialized_) {
        frame_count_++;
    }
}

void NullRenderer::Shutdown() {
    initialized_ = false;
}

std::string_view NullRenderer::GetBackendName() const {
    return "NullRenderer";
}

uint64_t NullRenderer::GetFrameCount() const {
    return frame_count_;
}

} // namespace aoe2::renderer
