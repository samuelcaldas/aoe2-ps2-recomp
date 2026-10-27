#include <aoe2/renderer/gles_renderer.hpp>
#include <cassert>
#include <iostream>
#include <vector>

namespace {

void Check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

void TestLifecycleAndState() {
    aoe2::renderer::GlesRenderer renderer;
    aoe2::renderer::FramebufferInfo info{1280, 720};

    Check(renderer.Initialize(info), "initialize 1280x720");
    Check(renderer.GetBackendName() == "GlesRenderer", "backend name is GlesRenderer");
    Check(renderer.GetFrameCount() == 0, "frame count starts at 0");

    std::vector<uint32_t> pixels(16 * 16, 0xFFFFFFFF);
    Check(renderer.UploadTexture(101, 16, 16, pixels.data()), "upload texture 101");
    Check(renderer.HasTexture(101), "has texture 101");

    renderer.BeginFrame();
    renderer.DrawSprite(101, {0, 0, 16, 16}, {100, 100, 32, 32}, 0xFFFFFFFF);
    Check(renderer.GetDrawCallCount() == 1, "draw call count is 1");
    renderer.EndFrame();

    Check(renderer.GetFrameCount() == 1, "frame count is 1 after EndFrame");

    renderer.Shutdown();
    Check(!renderer.IsInitialized(), "renderer is shut down");
}

} // namespace

int main() {
    TestLifecycleAndState();
    std::cout << "gles_renderer_tests passed\n";
    return 0;
}
