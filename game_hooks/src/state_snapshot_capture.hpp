// Reads a module's work area from guest RAM and logs it through state_snapshot.hpp.
// Call OnModuleUpdate once per completed per-frame update of the module (original or native).
#pragma once

#include "guest_rdram_access.hpp"
#include "input_synthetic.hpp"
#include "state_snapshot.hpp"

#include <iostream>
#include <vector>

namespace aoe2::game_hooks::diagnostics {

inline void OnModuleUpdate(uint8_t* rdram, synthetic::Module module) {
    static bool announced = false;
    if (!announced) {
        std::cerr << FormatConfig(Enabled()) << std::endl;
        announced = true;
    }
    synthetic::NoteUpdate(module);
    if (!Enabled()) return;
    static uint64_t counts[2] = {};
    const StateRegion& region = module == synthetic::Module::kMouse ? kMouseRegion : kKeyboardRegion;
    const uint64_t n = ++counts[static_cast<int>(module)];
    if (!ShouldSnapshot(n)) return;
    uint32_t offset = 0;
    bool scratch = false;
    if (!rdram || !resolveEeGuestRange(region.address, region.size, offset, scratch) || scratch) {
        std::cerr << FormatSkip(region, n, "invalid-range") << std::endl;
        return;
    }
    std::cerr << FormatSnapshot(region, n, synthetic::LogicalSteps(module).load(), rdram + offset) << std::endl;
}

}  // namespace aoe2::game_hooks::diagnostics
