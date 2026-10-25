#include "mc_snapshot.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void mc_region_is_valid() {
    using namespace aoe2::game_hooks::diagnostics;
    Check(kMcRegion.address == 0x004E2CB8, "mc region starts at D_4E2CB8");
    Check(kMcRegion.size >= 0x40, "mc region covers work area");
}

void formatting_snapshots_produces_expected_output() {
    using namespace aoe2::game_hooks::diagnostics;
    std::vector<uint8_t> rdram(32 * 1024 * 1024, 0);
    rdram[0x004E2CB8] = 0xAA;
    rdram[0x004E2CB8 + 1] = 0xBB;

    const std::string mc_snap = FormatMcSnapshot(rdram.data(), 1, 1);
    Check(mc_snap.find("label=mc") != std::string::npos, "mc label present");
    Check(mc_snap.find("n=1") != std::string::npos, "step n=1 present");
}
}  // namespace

int main() {
    mc_region_is_valid();
    formatting_snapshots_produces_expected_output();
    std::cout << "mc snapshot tests passed\n";
    return 0;
}
