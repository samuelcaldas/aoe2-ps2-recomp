#include "map_view_observation_private.hpp"
#include <cstring>
#include <stdexcept>
#include <vector>

namespace aoe2::game_hooks::map_view::detail {
void RedirectNativeViewport(uint8_t*, R5900Context*, const State&, const Relocation&);
bool ValidateViewport(uint8_t*, R5900Context*, const Relocation&);
}

int main() {
    using namespace aoe2::game_hooks::map_view;
    using namespace aoe2::game_hooks::map_view::detail;
    std::vector<uint8_t> ram(0x4000000);
    auto put = [&](uint32_t at, uint32_t value) { std::memcpy(ram.data() + at, &value, 4); };
    State state;
    state.mode = aoe2::game_hooks::MapViewMode::Native;
    state.lease = {0x3000000, 0x800000, 1};
    auto& r = state.relocation;
    r.area = 0x1000; r.base = state.lease.base;
    r.target_w = 1280; r.target_h = 534;
    r.layout.pitch = 1280; r.layout.visible_offset = 6416;
    constexpr uint32_t map = 0x3000, minimap = 0x4000, target = 0x5000;
    put(r.area + 0x40, map);
    put(map + 0x38, 544); put(map + 0x3c, 1280); put(map + 0x54, 0x3000010);
    put(map + 0x98, 0x800); put(minimap + 0x98, 0x800);
    R5900Context ctx;
    ctx.r[4] = _mm_set_epi64x(0, minimap); ctx.r[5] = _mm_set_epi64x(0, target);
    RedirectNativeViewport(ram.data(), &ctx, state, r);
    if (Word(ram.data(), map + 0x38) != 544 || ValidateViewport(ram.data(), &ctx, r))
        throw std::runtime_error("minimap changed or matched the map viewport");
    ctx.r[4] = _mm_set_epi64x(0, map);
    RedirectNativeViewport(ram.data(), &ctx, state, r);
    if (!ValidateViewport(ram.data(), &ctx, r) || Word(ram.data(), map + 0x38) != 534)
        throw std::runtime_error("map viewport was not redirected");
}
