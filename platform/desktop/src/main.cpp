#include <aoe2/core/target.hpp>
#include <aoe2/assets/asset_path_resolver.hpp>
#include <aoe2/assets/asset_resolver.hpp>
#include <aoe2/assets/loose_hd_asset_provider.hpp>
#include <aoe2/input/actions.hpp>
#include <aoe2/platform/desktop.hpp>
#include <aoe2/renderer/gles_renderer.hpp>
#include <aoe2/renderer/renderer.hpp>
#include <aoe2/runtime/guest_memory.hpp>

#include <cassert>
#include <iostream>

int main() {
    std::cout << "=== AoE2 PS2 Recompilation Platform Host Smoke Test ===" << std::endl;
    std::cout << "Target: " << aoe2::core::TargetInfo::kTitle << std::endl;
    std::cout << "Serial: " << aoe2::core::TargetInfo::kSerial << std::endl;
    std::cout << "Expected SHA-1: " << aoe2::core::TargetInfo::kExpectedSha1 << std::endl;

    // 1. Initialize desktop services
    aoe2::platform::desktop::DesktopPlatformServices services;
    services.LogInfo("Initializing platform services...");

    // 2. Initialize guest memory
    aoe2::runtime::GuestMemory memory;
    if (!memory.Initialize()) {
        services.LogError("Failed to initialize guest memory!");
        return 1;
    }
    services.LogInfo("Guest memory (64 MiB) allocated successfully.");

    // Simple read/write test
    memory.Write32(0x1000, 0x12345678);
    assert(memory.Read32(0x1000) == 0x12345678);

    // 3. Initialize Null Renderer
    aoe2::renderer::NullRenderer renderer;
    aoe2::renderer::FramebufferInfo fb_info{640, 480};
    if (!renderer.Initialize(fb_info)) {
        services.LogError("Failed to initialize renderer!");
        return 1;
    }
    services.LogInfo("Renderer initialized successfully.");

    renderer.BeginFrame();
    renderer.EndFrame();
    assert(renderer.GetFrameCount() == 1);
    services.LogInfo("Frame cycle completed.");

    // 4. Test GLES Renderer (Switch-ready 1280x720)
    aoe2::renderer::GlesRenderer gles_renderer;
    aoe2::renderer::FramebufferInfo gles_fb_info{1280, 720};
    if (!gles_renderer.Initialize(gles_fb_info)) {
        services.LogError("Failed to initialize GLES renderer!");
        return 1;
    }
    gles_renderer.BeginFrame();
    std::vector<uint32_t> test_pixels(16 * 16, 0xFFFFFFFF);
    gles_renderer.UploadTexture(1, 16, 16, test_pixels.data());
    gles_renderer.DrawSprite(1, {0, 0, 16, 16}, {0, 0, 16, 16}, 0xFFFFFFFF);
    gles_renderer.EndFrame();
    assert(gles_renderer.GetFrameCount() == 1);
    assert(gles_renderer.GetDrawCallCount() == 1);
    services.LogInfo("GLES Renderer (1280x720) initialized and tested successfully.");

    // 5. Test HD Asset Provider & Resolver
    aoe2::assets::LooseHdAssetProvider asset_provider;
    aoe2::assets::AssetPathResolver path_resolver;
    const std::string root_str = path_resolver.ResolveRoot().string();
    asset_provider.Initialize(root_str);
    aoe2::assets::AssetResolver asset_resolver(asset_provider);
    services.LogInfo("Asset resolver initialized successfully.");

    // 6. Test input sink
    aoe2::platform::desktop::ActionRecorderInputSink input_sink;
    aoe2::input::ActionEvent evt{
        .type = aoe2::input::ActionType::PrimaryClick,
        .x = 320.0f,
        .y = 240.0f,
        .delta_x = 0.0f,
        .delta_y = 0.0f,
        .timestamp_us = services.GetMicroseconds()
    };
    input_sink.SubmitEvent(evt);
    assert(input_sink.GetEvents().size() == 1);
    assert(input_sink.GetEvents()[0].type == aoe2::input::ActionType::PrimaryClick);
    services.LogInfo("Input event recorded successfully.");

    gles_renderer.Shutdown();
    renderer.Shutdown();
    services.LogInfo("Smoke test passed clean.");
    return 0;
}
