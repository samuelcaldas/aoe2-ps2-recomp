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
    Check(renderer.GetContext().IsActive(), "GLES context is active");
    Check(renderer.GetFramebufferInfo().width == 1280 && renderer.GetFramebufferInfo().height == 720,
          "framebuffer dimensions are retained");
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

    Check(renderer.Initialize({1920, 1080}), "reinitialize at 1920x1080");
    Check(renderer.GetContext().GetWidth() == 1920 && renderer.GetContext().GetHeight() == 1080,
          "resizing recreates the GLES context at 1080p");

    renderer.Shutdown();
    Check(!renderer.IsInitialized(), "renderer is shut down");
}

void TestTextureOverwritingAndCleanup() {
    aoe2::renderer::GlesRenderer renderer;
    renderer.Initialize({1280, 720});
    std::vector<uint32_t> p1(4 * 4, 0x11111111);
    std::vector<uint32_t> p2(4 * 4, 0x22222222);
    renderer.UploadTexture(1, 4, 4, p1.data());
    // Overwrite texture
    renderer.UploadTexture(1, 4, 4, p2.data());
    Check(renderer.HasTexture(1), "still has texture 1");
    // Explicit DeleteTexture
    renderer.DeleteTexture(1);
    Check(!renderer.HasTexture(1), "texture 1 deleted");
}

void TestCommandsAndText() {
    aoe2::renderer::GlesRenderer renderer;
    Check(renderer.Initialize({1920, 1080}), "initialize 1920x1080");
    Check(renderer.GetContext().IsActive(), "1080p GLES context is active");
    Check(renderer.GetFramebufferInfo().width == 1920 && renderer.GetFramebufferInfo().height == 1080,
          "1080p framebuffer dimensions are retained");
    std::vector<uint32_t> pixels(32 * 32, 0xAABBCCDD);
    renderer.UploadTexture(42, 32, 32, pixels.data());

    renderer.BeginFrame();
    renderer.DrawSprite(42, {0, 0, 32, 32}, {10, 20, 64, 64}, 0xFFFFFFFF);
    Check(renderer.GetSpriteCommands().size() == 1, "has 1 sprite command before text");

    renderer.DrawText("Wood: 200", 15.0f, 25.0f, 20.0f, 0xFFFFFFFF);

    const auto& spriteCmds = renderer.GetSpriteCommands();
    Check(spriteCmds.size() == 10, "has 10 sprite commands (1 sprite + 9 font glyphs)");
    Check(spriteCmds[0].texture_id == 42, "sprite command texture id is 42");
    Check(spriteCmds[0].dst_rect.width == 64.0f, "sprite dst width is 64");
    Check(spriteCmds[1].texture_id == aoe2::renderer::kFontTextureId, "glyph sprite uses font texture");

    const auto& textCmds = renderer.GetTextCommands();
    Check(textCmds.size() == 1, "has 1 text command");
    Check(textCmds[0].text == "Wood: 200", "text matches");
    Check(textCmds[0].x == 15.0f, "text x matches");

    const auto* tex = renderer.GetTexture(42);
    Check(tex != nullptr, "texture 42 found");
    Check(tex->width == 32 && tex->height == 32, "texture 42 dimensions match");
    Check(tex->pixels[0] == 0xAABBCCDD, "texture 42 pixel matches");

    renderer.EndFrame();

    renderer.BeginFrame();
    Check(renderer.GetSpriteCommands().empty(), "sprite commands cleared on BeginFrame");
    Check(renderer.GetTextCommands().empty(), "text commands cleared on BeginFrame");
    renderer.EndFrame();
}

void TestInvalidFramebufferRejected() {
    aoe2::renderer::GlesRenderer renderer;
    Check(!renderer.Initialize({0, 1080}), "zero-width framebuffer is rejected");
    Check(!renderer.IsInitialized(), "failed initialization leaves renderer shut down");
}

void TestMultipleTextures() {
    aoe2::renderer::GlesRenderer renderer;
    renderer.Initialize({640, 480});
    std::vector<uint32_t> p1(8 * 8, 0xFF0000FF);
    std::vector<uint32_t> p2(8 * 8, 0x00FF00FF);
    renderer.UploadTexture(10, 8, 8, p1.data());
    renderer.UploadTexture(20, 8, 8, p2.data());

    renderer.BeginFrame();
    renderer.DrawSprite(10, {0, 0, 8, 8}, {0, 0, 16, 16}, 0xFFFFFFFF);
    renderer.DrawSprite(20, {0, 0, 8, 8}, {16, 0, 16, 16}, 0xFFFFFFFF);
    Check(renderer.GetSpriteCommands().size() == 2, "2 sprites drawn");
    Check(renderer.GetDrawCallCount() == 2, "draw call count is 2");
    renderer.EndFrame();
}

} // namespace

int main() {
    TestInvalidFramebufferRejected();
    TestLifecycleAndState();
    TestTextureOverwritingAndCleanup();
    TestCommandsAndText();
    TestMultipleTextures();
    std::cout << "gles_renderer_tests passed\n";
    return 0;
}
