"""Execute the GPU installer without a game build; keep asset/layout code real.

Only the external runtime/graphics and native-module boundaries are doubled.
The production installer is compiled byte-for-byte, not reimplemented here.
"""
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SUPPORT = r'''
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>
struct R5900Context {};
inline uint32_t getRegU32(R5900Context*, int) { return 0; }
inline const uint8_t* getConstMemPtr(uint8_t* ram, uint32_t address) { return ram + address; }
class PS2Runtime {
public:
    using RecompiledFunction = void (*)(uint8_t*, R5900Context*, PS2Runtime*);
    using PresentationCallback = void (*)(PS2Runtime&, void*);
    PresentationCallback presentation_callback = nullptr;
    RecompiledFunction lookupFunction(uint32_t) { return nullptr; }
    bool replaceFunction(uint32_t, RecompiledFunction) { return true; }
    void setPresentationCallback(PresentationCallback callback, void*) { presentation_callback = callback; }
    struct Memory {
        std::vector<uint8_t> bytes = std::vector<uint8_t>(0x2000000);
        uint8_t* getRDRAM() { return bytes.data(); }
    } memory_storage;
    Memory& memory() { return memory_storage; }
};
#define PS2_REGISTER_GAME_OVERRIDE(...)
namespace aoe2::game_hooks::gpu {
inline constexpr std::array<uint32_t, 7> entries{};
inline void Install(PS2Runtime&) {}
inline void SetNativeDrawInterceptor(bool (*)(uint8_t, uint32_t, uint32_t)) {}
}
namespace aoe2::game_hooks::diagnostics {
inline uint64_t gpu_snapshot_count = 0;
inline bool Enabled() { return false; }
inline void ObserveGpuState(uint8_t*, uint32_t, uint32_t) {}
}
namespace aoe2::game_hooks::native_input {
struct Switches { bool Enabled(const char*) const { return true; } };
inline Switches ReadSwitches() { return {}; }
}
'''

RAYLIB = r'''
#pragma once
struct Texture2D { unsigned int id; int width, height, mipmaps, format; };
struct Image { void* data; int width, height, mipmaps, format; };
struct Rectangle { float x, y, width, height; };
struct Vector2 { float x, y; };
struct Color { unsigned char r, g, b, a; };
inline constexpr Color WHITE{255, 255, 255, 255};
inline constexpr int PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 = 7;
inline constexpr int TEXTURE_FILTER_BILINEAR = 1;
inline constexpr int TEXTURE_WRAP_CLAMP = 1;
inline Texture2D LoadTextureFromImage(Image image) { return {1, image.width, image.height, 1, 7}; }
inline void SetTextureFilter(Texture2D, int) {}
inline void SetTextureWrap(Texture2D, int) {}
inline int GetScreenWidth() { return 1920; }
inline int GetScreenHeight() { return 1080; }
inline int presentation_draws = 0;
inline void DrawRectangle(int, int, int, int, Color) { ++presentation_draws; }
inline void DrawTexturePro(Texture2D, Rectangle, Rectangle, Vector2, float, Color) { ++presentation_draws; }
'''

HARNESS = r'''
#include <aoe2/input/hud_inverse_mapper.hpp>
#include <aoe2/assets/asset_resolver.hpp>
#include <aoe2/assets/loose_hd_asset_provider.hpp>
namespace aoe2::game_hooks::detail {
const aoe2::input::HudInverseMapper* active_mapper = nullptr;
void SetActiveHudMapper(const aoe2::input::HudInverseMapper* mapper) { active_mapper = mapper; }
}
#include "kcet_gpu_module.cpp"
#include "raylib.h"
#include <sstream>
int main(int, char** arguments) {
    using aoe2::assets::AssetCategory;
    aoe2::assets::LooseHdAssetProvider provider;
    if (!provider.Initialize("game/SLES-50282/assets")) return 2;
    aoe2::assets::AssetResolver resolver(provider);
    if (!resolver.LoadPalette(AssetCategory::Interface, 50588)) return 2;
    const auto fixture = resolver.ResolveSprite(AssetCategory::Interface, 50102, 0);
    if (!fixture || fixture->width != 1280 || fixture->height != 1024) return 2;
    PS2Runtime runtime;
    const uint32_t active = 1;
    std::memcpy(runtime.memory().getRDRAM() + 0x0050A5A0, &active, sizeof(active));
    std::memcpy(runtime.memory().getRDRAM() + 0x004E3248, &active, sizeof(active));
    std::ostringstream log;
    auto* previous = std::cout.rdbuf(log.rdbuf());
    InstallNativeGpu(runtime);
    std::cout.rdbuf(previous);
    const std::string check(arguments[1]);
    if (runtime.presentation_callback) runtime.presentation_callback(runtime, nullptr);
    if (check == "overlay" && presentation_draws != 0) {
        std::cerr << "unmapped HD assets must not draw over the functional guest HUD\n";
        return 1;
    }
    if (check == "input" && aoe2::game_hooks::detail::active_mapper) {
        std::cerr << "guest fallback must not remap clicks to an undisplayed native HUD\n";
        return 1;
    }
    if (check == "status" && log.str().find("native GLES renderer and HD asset resolver active") != std::string::npos) {
        std::cerr << "initialization must not claim native HD/GLES rendering without a replacement\n";
        return 1;
    }
}
'''


class TestGpuPresentationFallback(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        cls.staging = Path(cls.directory.name)
        shutil.copyfile(ROOT / "game_hooks/src/kcet_gpu_module.cpp", cls.staging / "kcet_gpu_module.cpp")
        (cls.staging / "support.hpp").write_text(SUPPORT)
        for name in ("gpu_runtime_adapter.hpp", "gpu_snapshot.hpp", "game_overrides.h",
                     "native_input_module_support.hpp"):
            (cls.staging / name).write_text('#include "support.hpp"\n')
        (cls.staging / "raylib.h").write_text(RAYLIB)
        (cls.staging / "harness.cpp").write_text(HARNESS)
        cls.write_unmapped_assets()
        cls.executable = cls.staging / "gpu-fallback-test"
        sources = [
            "assets/src/asset_path_resolver.cpp", "assets/src/asset_resolver.cpp",
            "assets/src/loose_hd_asset_provider.cpp", "assets/src/jasc_palette.cpp",
            "assets/src/slp_decoder.cpp", "renderer/src/gles_renderer.cpp",
            "renderer/src/gles_context.cpp", "renderer/src/gles_shader.cpp",
            "renderer/src/gles_batch.cpp", "renderer/src/gles_font.cpp",
            "renderer/src/hud_layout.cpp", "input/src/hud_inverse_mapper.cpp",
        ]
        command = ["g++", "-std=c++20", f"-I{cls.staging}"]
        command += [f"-I{ROOT / name / 'include'}" for name in ("assets", "core", "renderer", "input")]
        command += [str(cls.staging / "harness.cpp"), *[str(ROOT / path) for path in sources],
                    "-o", str(cls.executable)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    @classmethod
    def write_unmapped_assets(cls):
        """A valid synthetic SLP/palette must not count as a verified HUD mapping."""
        interface = cls.staging / "game/SLES-50282/assets/interface"
        interface.mkdir(parents=True)
        palette = "JASC-PAL\n0100\n256\n" + "255 0 255\n" * 256
        (interface / "50588.bina").write_text(palette)
        # Every row paints 1280 pixels; row command offsets intentionally share data.
        commands = bytes([0x07, 255, 1] * 5 + [0x57, 1, 0x0F])
        header = b"2.0N" + struct.pack("<I", 1) + bytes(24)
        frame = struct.pack("<IIIIiiii", 4160, 64, 0, 0, 1280, 1024, 0, 0)
        outlines = bytes(4096)
        offsets = struct.pack("<I", 8256) * 1024
        (interface / "50102.slp").write_bytes(header + frame + outlines + offsets + commands)

    def check_fallback(self, check):
        result = subprocess.run([str(self.executable), check], cwd=self.staging,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_unmapped_assets_do_not_cover_functional_guest_hud(self):
        self.check_fallback("overlay")

    def test_fallback_does_not_activate_unrendered_hud_hit_regions(self):
        self.check_fallback("input")

    def test_initialization_does_not_claim_native_hd_rendering(self):
        self.check_fallback("status")


if __name__ == "__main__":
    unittest.main()
