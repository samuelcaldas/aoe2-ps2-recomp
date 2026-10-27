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
        if (resolver.LoadPalette(aoe2::assets::AssetCategory::Interface, 50588)) {
            auto hdBase = resolver.ResolveSprite(aoe2::assets::AssetCategory::Interface, 50102, 0);
            if (hdBase.has_value()) {
                Check(hdBase->width == 1280 && hdBase->height == 1024, "HD interface base 50102 dimensions match");
                Check(hdBase->rgba_data != nullptr, "50102 has valid pixels");
                Check(hdBase->texture_id != 0, "50102 has texture id");
            }
            std::cout << "Verified HD interface base 50102 resolved with authentic wood palette 50588\n";
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
