#include "gpu_runtime_adapter.hpp"
#include "gpu_snapshot.hpp"
#include "game_overrides.h"
#include "native_input_module_support.hpp"
#include <aoe2/assets/asset_path_resolver.hpp>
#include <aoe2/assets/asset_resolver.hpp>
#include <aoe2/assets/loose_hd_asset_provider.hpp>
#include <aoe2/input/hud_inverse_mapper.hpp>
#include <aoe2/renderer/gles_renderer.hpp>
#include <aoe2/renderer/hud_layout.hpp>
#include <iostream>
#include <memory>
#include <unordered_map>

#if __has_include("raylib.h")
#include "raylib.h"
#define AOE2_HAS_RAYLIB 1
#endif

namespace aoe2::game_hooks::detail {
void SetActiveHudMapper(const aoe2::input::HudInverseMapper* mapper);
}

namespace {
std::array<PS2Runtime::RecompiledFunction, 7> originals{};
std::unique_ptr<aoe2::assets::LooseHdAssetProvider> s_hd_provider;
std::unique_ptr<aoe2::assets::AssetResolver> s_hd_resolver;
std::unique_ptr<aoe2::renderer::GlesRenderer> s_gles_renderer;
std::unique_ptr<aoe2::renderer::HudLayout> s_hud_layout;
std::unique_ptr<aoe2::input::HudInverseMapper> s_hud_mapper;

#if defined(AOE2_HAS_RAYLIB)
struct HostTextureRecord {
    Texture2D tex{};
    bool valid{false};
};
std::unordered_map<uint32_t, HostTextureRecord> s_host_textures;

Texture2D GetOrCreateHostTexture(uint32_t texId, const aoe2::assets::ResolvedSprite& sprite) {
    auto it = s_host_textures.find(texId);
    if (it != s_host_textures.end() && it->second.valid) {
        return it->second.tex;
    }
    Image img{
        .data = const_cast<uint32_t*>(sprite.rgba_data),
        .width = sprite.width,
        .height = sprite.height,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8,
    };
    Texture2D tex = LoadTextureFromImage(img);
    SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    SetTextureWrap(tex, TEXTURE_WRAP_CLAMP);
    s_host_textures[texId] = {tex, true};
    return tex;
}

bool IsGameplayActive(PS2Runtime& runtime) {
    uint8_t* rdram = runtime.memory().getRDRAM();
    if (!rdram) return false;
    uint32_t visibleMapPtr = 0;
    // D_50A5A0 is the active visible map grid (0 in menus, non-null during gameplay)
    if (const uint8_t* ptr = getConstMemPtr(rdram, 0x0050A5A0)) {
        std::memcpy(&visibleMapPtr, ptr, sizeof(visibleMapPtr));
    }
    uint32_t cursorController = 0;
    // D_004E3248 is the in-game zone cursor controller (0 during briefing slides/menus)
    if (const uint8_t* ptr = getConstMemPtr(rdram, 0x004E3248)) {
        std::memcpy(&cursorController, ptr, sizeof(cursorController));
    }
    return visibleMapPtr != 0 && cursorController != 0;
}

void NativePresentationCallback(PS2Runtime& runtime, void*) {
    if (!s_hd_resolver) return;
    const float screenW = static_cast<float>(GetScreenWidth());
    const float screenH = static_cast<float>(GetScreenHeight());
    if (screenW < 800.0f) return;

    // Only render in-game HD HUD during active gameplay (not in menus, loadings, or briefings)
    if (!IsGameplayActive(runtime)) {
        return;
    }

    static bool gameplay_logged = false;
    if (!gameplay_logged) {
        std::cout << "[aoe2] NativePresentationCallback active in gameplay screen=" << screenW << "x" << screenH << std::endl;
        gameplay_logged = true;
    }

    // Proportional dimensions matching the 640x448 playfield scaled to host height
    const float playfieldScale = screenH / 448.0f;
    const float playfieldW = 640.0f * playfieldScale;
    const float playfieldX = (screenW - playfieldW) * 0.5f;

    // Load HD Base Interface Asset 50102 (1280x1024 authentic wood interface)
    auto hdInterface = s_hd_resolver->ResolveSprite(aoe2::assets::AssetCategory::Interface, 50102, 0);
    if (!hdInterface.has_value() || hdInterface->width <= 0 || hdInterface->height <= 0) {
        return;
    }
    Texture2D tex = GetOrCreateHostTexture(hdInterface->texture_id, *hdInterface);

    // 1. Render Top Resource Bar (top 48px of 50102) covering low-res PS2 bar
    const float topH = 34.0f * playfieldScale;
    DrawRectangle(0, 0, static_cast<int>(screenW), static_cast<int>(topH), Color{48, 28, 14, 255});
    Rectangle srcTop{0.0f, 0.0f, static_cast<float>(tex.width), 48.0f};
    Rectangle dstTop{playfieldX, 0.0f, playfieldW, topH};
    DrawTexturePro(tex, srcTop, dstTop, Vector2{0.0f, 0.0f}, 0.0f, WHITE);

    // 2. Render Bottom Command Panel (bottom 150px of 50102) covering low-res PS2 bar
    const float botY = 300.0f * playfieldScale;
    const float botH = screenH - botY;
    DrawRectangle(0, static_cast<int>(botY), static_cast<int>(screenW), static_cast<int>(botH), Color{48, 28, 14, 255});
    Rectangle srcBot{0.0f, static_cast<float>(tex.height - 150), static_cast<float>(tex.width), 150.0f};
    Rectangle dstBot{playfieldX, botY, playfieldW, botH};
    DrawTexturePro(tex, srcBot, dstBot, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
}
#endif

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
            const bool palOk = s_hd_resolver->LoadPalette(aoe2::assets::AssetCategory::Interface, 50588);
            std::cout << "[aoe2] palette 50588 loaded=" << (palOk ? "yes" : "no") << " root=" << root << '\n';
            s_gles_renderer = std::make_unique<aoe2::renderer::GlesRenderer>();
            s_gles_renderer->Initialize({1920, 1080});
            std::vector<aoe2::renderer::HudRegionSpec> hud_specs{
                {aoe2::renderer::HudRegionRole::ResourceBar, aoe2::renderer::HudAnchor::Top, {0, 0, 640, 32}, 1920, 32, 0, 0},
                {aoe2::renderer::HudRegionRole::CommandBar, aoe2::renderer::HudAnchor::Bottom, {0, 300, 640, 148}, 1920, 148, 0, 0},
            };
            s_hud_layout = std::make_unique<aoe2::renderer::HudLayout>(
                aoe2::renderer::HudLayout::ForExtent({640, 448}, {1920, 1080}, hud_specs));
            s_hud_mapper = std::make_unique<aoe2::input::HudInverseMapper>(s_hud_layout->regions());
            aoe2::game_hooks::detail::SetActiveHudMapper(s_hud_mapper.get());
#if defined(AOE2_HAS_RAYLIB)
            runtime.setPresentationCallback(NativePresentationCallback, nullptr);
#endif
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
