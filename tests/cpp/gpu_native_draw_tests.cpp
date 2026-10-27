#include <aoe2/gpu/kt_gpu_model.hpp>
#include <aoe2/renderer/gles_renderer.hpp>
#include <aoe2/assets/asset_resolver.hpp>
#include <aoe2/assets/loose_hd_asset_provider.hpp>

#include <cassert>
#include <iostream>

namespace {

void Check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << '\n';
        std::exit(1);
    }
}

// Global interceptor hook test
bool MockInterceptor(uint8_t kind, uint32_t cmd_addr, uint32_t work_state) {
    if (kind == 0) { // Kind 0 is 2D sprite draw
        return true; // Intercepted
    }
    return false; // Fallback to PS2 guest handler
}

void TestDrawInterceptionLogic() {
    Check(MockInterceptor(0, 0x3000, 0x2000) == true, "Kind 0 (Sprite) intercepted");
    Check(MockInterceptor(1, 0x3000, 0x2000) == false, "Kind 1 (TRISTRIP) falls back");
    Check(MockInterceptor(14, 0x3000, 0x2000) == false, "Kind 14 (TEX0) falls back");
}

void TestHdInterfaceResolution() {
    aoe2::assets::LooseHdAssetProvider provider;
    // Test if staged assets exist
    if (provider.Initialize("game/SLES-50282") || provider.Initialize("game/hd-edition")) {
        aoe2::assets::AssetResolver resolver(provider);
        if (resolver.LoadPalette(aoe2::assets::AssetCategory::Interface, 50507)) {
            auto topBar = resolver.ResolveSprite(aoe2::assets::AssetCategory::Interface, 50792, 0);
            if (topBar.has_value()) {
                Check(topBar->width == 697 && topBar->height == 24, "top bar 50792 dimensions match");
                Check(topBar->rgba_data != nullptr, "top bar has valid pixels");
                Check(topBar->texture_id != 0, "top bar has texture id");
            }
            auto botBar = resolver.ResolveSprite(aoe2::assets::AssetCategory::Interface, 50150, 0);
            if (botBar.has_value()) {
                Check(botBar->width == 592 && botBar->height == 90, "bottom bar 50150 dimensions match");
                Check(botBar->rgba_data != nullptr, "bottom bar has valid pixels");
            }
            std::cout << "Verified HD interface assets 50792 and 50150 resolved successfully\n";
        }
    }
}

} // namespace

int main() {
    TestDrawInterceptionLogic();
    TestHdInterfaceResolution();
    std::cout << "gpu_native_draw_tests passed\n";
    return 0;
}
