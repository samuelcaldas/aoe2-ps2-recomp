#include <aoe2/renderer/gles_font.hpp>
#include <aoe2/renderer/gles_renderer.hpp>
#include <aoe2/renderer/gles_types.hpp>
#include "gles_font_data.hpp"
#include <vector>

namespace aoe2::renderer {

namespace {

void RenderGlyphRow(uint32_t* dst, uint8_t bits, uint32_t base_x) {
    for (uint32_t b = 0; b < 8; ++b) {
        if ((bits >> (7 - b)) & 1) dst[base_x + b] = 0xFFFFFFFF;
    }
}

void BuildAtlas(std::vector<uint32_t>& pixels) {
    for (uint32_t g = 0; g < 96; ++g) {
        uint32_t col = (g % 16) * 8;
        uint32_t row = (g / 16) * 8;
        for (uint32_t y = 0; y < 8; ++y) {
            uint8_t bits = detail::kFont8x8[g * 8 + y];
            RenderGlyphRow(&pixels[(row + y) * 128], bits, col);
        }
    }
}

} // namespace

bool GlesFont::Initialize(GlesRenderer& renderer) {
    if (initialized_) return true;
    std::vector<uint32_t> pixels(128 * 64, 0);
    BuildAtlas(pixels);
    initialized_ = renderer.UploadTexture(kFontTextureId, 128, 64, pixels.data());
    return initialized_;
}

void GlesFont::DrawString(GlesRenderer& renderer, std::string_view text,
                          float x, float y, float size, uint32_t color) {
    if (!initialized_ && !Initialize(renderer)) return;
    float cur_x = x;
    for (char c : text) {
        if (c < 32 || c > 127) {
            cur_x += size * 0.6f;
            continue;
        }
        uint32_t idx = static_cast<uint32_t>(c - 32);
        float u = static_cast<float>((idx % 16) * 8);
        float v = static_cast<float>((idx / 16) * 8);
        renderer.DrawSprite(kFontTextureId, {u, v, 8.0f, 8.0f}, {cur_x, y, size, size}, color);
        cur_x += size * 0.65f;
    }
}

} // namespace aoe2::renderer
