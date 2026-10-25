#include "hd_snapshot.hpp"

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

void hd_region_is_valid() {
    using namespace aoe2::game_hooks::diagnostics;
    Check(kHdRegion.address == 0x004EA180, "hd region starts at D_4EA180");
    Check(kHdRegion.size >= 0x40, "hd region covers work area");
}

void formatting_snapshots_produces_expected_output() {
    using namespace aoe2::game_hooks::diagnostics;
    std::vector<uint8_t> rdram(32 * 1024 * 1024, 0);
    rdram[0x004EA180] = 0x11;
    rdram[0x004EA180 + 1] = 0x22;

    const std::string hd_snap = FormatHdSnapshot(rdram.data(), 1, 1);
    Check(hd_snap.find("label=hd") != std::string::npos, "hd label present");
    Check(hd_snap.find("n=1") != std::string::npos, "step n=1 present");
}
}  // namespace

int main() {
    hd_region_is_valid();
    formatting_snapshots_produces_expected_output();
    std::cout << "hd snapshot tests passed\n";
    return 0;
}
