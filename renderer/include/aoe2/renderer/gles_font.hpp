#pragma once

#include <cstdint>
#include <string_view>

namespace aoe2::renderer {

class GlesRenderer;

class GlesFont {
public:
    GlesFont() = default;

    bool Initialize(GlesRenderer& renderer);
    void Teardown() { initialized_ = false; }
    void DrawString(GlesRenderer& renderer, std::string_view text,
                    float x, float y, float size, uint32_t color);

    bool IsInitialized() const { return initialized_; }

private:
    bool initialized_{false};
};

} // namespace aoe2::renderer
