#pragma once
#include <aoe2/game_hooks/map_view_state.hpp>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>
class PS2Runtime;
namespace aoe2::game_hooks::map_view {
// STEP2 is deliberately fixed. Live parity evidence is required before changing this phase.
enum class Phase { LegacyRelocation };
inline constexpr Phase kPhase = Phase::LegacyRelocation;
struct Relocation {
    uint32_t panel = 0, area = 0, base = 0;
    uint64_t generation = 0;
    IndexedMapSurfaceLayout layout{};
    void clear() { *this = {}; }
    void begin(uint32_t p, uint32_t w, uint32_t h, uint32_t b, uint32_t capacity, uint64_t g) {
        clear();
        if (w != 640 || h != 262 || !p || !g || uint64_t(b)+capacity > 0x4000000)
            throw std::invalid_argument("STEP2 requires a valid lease and legacy 640x262 panel");
        layout = IndexedMapSurfaceLayout::Calculate(w, h, capacity);
        panel = p; base = b; generation = g;
    }
    uint32_t redirect(uint32_t entry, uint32_t ra, uint32_t p, uint32_t a,
                      uint32_t w, uint32_t h, bool valid) {
        if (!valid || !generation || p != panel || !a || w != 640 || h != 262) return 0;
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
};
// Call only at guest execution safe points. Teardown requires guest execution stopped;
// arena addresses retained in guest objects must never be used after teardown/reset.
void Install(PS2Runtime&);
void Teardown(PS2Runtime&);
std::optional<Snapshot> Capture(PS2Runtime&);
}
