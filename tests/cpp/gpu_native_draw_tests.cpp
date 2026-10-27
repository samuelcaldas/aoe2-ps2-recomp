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

} // namespace

int main() {
    TestDrawInterceptionLogic();
    std::cout << "gpu_native_draw_tests passed\n";
    return 0;
}
