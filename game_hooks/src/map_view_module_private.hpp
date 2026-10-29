#pragma once
#include <aoe2/game_hooks/map_view_state.hpp>
#include <aoe2/renderer/map_view_layout.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

class PS2Runtime;

namespace aoe2::game_hooks::map_view {

enum class Phase { LegacyRelocation, NativeEnlargement };
inline constexpr Phase kPhase = Phase::NativeEnlargement;

struct Relocation {
    uint32_t panel = 0, area = 0, base = 0;
    uint32_t target_w = 640, target_h = 262;
    uint64_t generation = 0;
    IndexedMapSurfaceLayout layout{};
    renderer::MapViewLayout active_layout{};

    void clear() {
        panel = 0; area = 0; base = 0;
        target_w = 640; target_h = 262;
        generation = 0;
        layout = {};
        active_layout = {};
    }

    void begin(uint32_t p, uint32_t w, uint32_t h, uint32_t b, uint32_t capacity, uint64_t g,
               renderer::MapViewLayout mvl = {}) {
        clear();
        if (w == 0 || w > 1920 || h == 0 || h > 917 || !p || !g || uint64_t(b) + capacity > 0x4000000)
            throw std::invalid_argument("invalid lease or surface bounds (height must not exceed 917 rows)");
        layout = IndexedMapSurfaceLayout::Calculate(w, h, capacity);
        panel = p; base = b; generation = g;
        target_w = w; target_h = h;
        active_layout = mvl;
    }

    uint32_t redirect(uint32_t entry, uint32_t ra, uint32_t p, uint32_t a,
                      uint32_t w, uint32_t h, bool valid) {
        if (!valid || !generation || p != panel || !a || w != target_w || h != target_h) return 0;
        if (!((entry == 0x1ab7e0 && ra == 0x31a488) ||
              (entry == 0x1abac0 && ra == 0x31a4b4))) return 0;
        if (area && area != a) return 0;
        area = a;
        return base + 16; // Original routines add the five top rows themselves.
    }
};

struct Snapshot {
    uint64_t generation = 0, submission = 0, palette_calls = 0;
    uint32_t panel = 0, area = 0, viewport = 0, source = 0;
    uint32_t palette_slot = 0, palette_source = 0;
    uint64_t indices_hash = 0, palette_hash = 0;
    uint32_t width = 640, height = 262, pitch = 640, upload_rows = 264;
    std::vector<uint8_t> indices; // Immutable owned bytes, including rounded upload rows.
    std::array<uint8_t, 1024> palette{};
    renderer::MapViewLayout layout{};
};

// Call only at guest execution safe points. Teardown requires guest execution stopped;
// arena addresses retained in guest objects must never be used after teardown/reset.
void Install(PS2Runtime&);
void Teardown(PS2Runtime&);
std::optional<Snapshot> Capture(PS2Runtime&);
std::optional<renderer::MapViewLayout> ActiveLayout(PS2Runtime&);
std::optional<renderer::MapViewLayout> ActiveLayout();

}  // namespace aoe2::game_hooks::map_view

