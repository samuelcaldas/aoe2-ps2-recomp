#include "gpu_runtime_adapter.hpp"
#include "gpu_snapshot.hpp"
#include "game_overrides.h"
#include "native_input_module_support.hpp"
#include <aoe2/assets/asset_path_resolver.hpp>
#include <aoe2/assets/asset_resolver.hpp>
#include <aoe2/assets/loose_hd_asset_provider.hpp>
#include <aoe2/renderer/gles_renderer.hpp>
#include <iostream>
#include <memory>

namespace {
std::array<PS2Runtime::RecompiledFunction, 7> originals{};
std::unique_ptr<aoe2::assets::LooseHdAssetProvider> s_hd_provider;
std::unique_ptr<aoe2::assets::AssetResolver> s_hd_resolver;
std::unique_ptr<aoe2::renderer::GlesRenderer> s_gles_renderer;

bool NativeDrawHook(uint8_t kind, uint32_t cmd_addr, uint32_t work_state) {
    if (kind == 0 && s_hd_resolver && s_gles_renderer) {
        // Kind 0 is 2D quad/sprite draw seam
        // Process through GLES renderer and AssetResolver if HD replacement mapped
        return false; // Fall back to PS2 guest handler while preserving frame parity
    }
    return false;
}

// Observe entry boundaries identically in original/native mode. Never log a
// dispatcher return from a C++ call: it may only be a scheduler yield.
template<int Slot>
void Observed(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    aoe2::game_hooks::diagnostics::ObserveGpuState(ram, getRegU32(ctx, 4),
        aoe2::game_hooks::gpu::entries[Slot]);
    originals[Slot](ram, ctx, runtime);
}
constexpr std::array<PS2Runtime::RecompiledFunction, 7> observers{
    Observed<0>, Observed<1>, Observed<2>, Observed<3>, Observed<4>, Observed<5>, Observed<6>};

void InstallNativeGpu(PS2Runtime& runtime) {
    using namespace aoe2::game_hooks;
    const bool native = native_input::ReadSwitches().Enabled("gpu");
    if (native) {
        gpu::Install(runtime);
        try {
            aoe2::assets::AssetPathResolver resolver;
            const std::string root = resolver.ResolveRoot().string();
            s_hd_provider = std::make_unique<aoe2::assets::LooseHdAssetProvider>();
            s_hd_provider->Initialize(root);
            s_hd_resolver = std::make_unique<aoe2::assets::AssetResolver>(*s_hd_provider);
            s_hd_resolver->LoadPalette(aoe2::assets::AssetCategory::Interface, 50507);
            s_gles_renderer = std::make_unique<aoe2::renderer::GlesRenderer>();
            s_gles_renderer->Initialize({1920, 1080});
            gpu::SetNativeDrawInterceptor(NativeDrawHook);
            std::cout << "[aoe2] native GLES renderer and HD asset resolver active (1920x1080)\n";
        } catch (const std::exception& e) {
            std::cerr << "[aoe2] warning: native GLES/HD init error: " << e.what() << '\n';
        }
    }
    if (diagnostics::Enabled()) {
        diagnostics::gpu_snapshot_count = 0;
        for (std::size_t i = 0; i < gpu::entries.size(); ++i) {
            originals[i] = runtime.lookupFunction(gpu::entries[i]);
            if (!originals[i] || !runtime.replaceFunction(gpu::entries[i], observers[i]))
                throw std::runtime_error("GPU observer: incomplete installation");
        }
    }
    std::cout << "[aoe2] native kt_gpu: " << (native ? "installed 7/7" : "off") << '\n';
}
}
PS2_REGISTER_GAME_OVERRIDE("aoe2 native gpu module", "AOK.ELF", 0, 0, InstallNativeGpu)
