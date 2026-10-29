#include <aoe2/game_hooks/map_view_state.hpp>
#include <stdexcept>

namespace aoe2::game_hooks {
IndexedMapSurfaceLayout IndexedMapSurfaceLayout::Calculate(uint64_t width, uint64_t height, uint64_t capacity) {
    if (width == 0 || width > 1920 || height == 0 || height > 1080)
        throw std::invalid_argument("indexed map surface must fit 1920x1080");
    const uint64_t pitch = (width + 15u) & ~uint64_t{15u};
    const uint64_t stride = 16u + pitch * (height + 10u) + 16u;
    if (stride * 2u > capacity)
        throw std::invalid_argument("indexed map surfaces exceed the exclusive arena");
    return {static_cast<uint32_t>(pitch), static_cast<uint32_t>(16u + pitch * 5u),
            static_cast<uint32_t>(stride), static_cast<uint32_t>(stride * 2u)};
}
}
