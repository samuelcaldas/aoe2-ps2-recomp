#include "gpu_snapshot.hpp"

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

void gpu_region_is_valid() {
    using namespace aoe2::game_hooks::diagnostics;
    Check(kGpuRegion.address == 0x0038C730, "gpu region starts at D_38C730");
    Check(kGpuRegion.size >= 0x40, "gpu region covers command table");
}

void formatting_snapshots_produces_expected_output() {
    using namespace aoe2::game_hooks::diagnostics;
    std::vector<uint8_t> rdram(32 * 1024 * 1024, 0);
    rdram[0x0038C730] = 0x55;
    rdram[0x0038C730 + 1] = 0x66;

    const std::string gpu_snap = FormatGpuSnapshot(rdram.data(), 1, 1);
    Check(gpu_snap.find("label=gpu") != std::string::npos, "gpu label present");
    Check(gpu_snap.find("n=1") != std::string::npos, "step n=1 present");
}
}  // namespace

int main() {
    gpu_region_is_valid();
    formatting_snapshots_produces_expected_output();
    std::cout << "gpu snapshot tests passed\n";
    return 0;
}
