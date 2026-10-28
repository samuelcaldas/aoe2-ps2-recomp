#include "hd_snapshot.hpp"
#include "file_snapshot.hpp"
#include "mc_snapshot.hpp"
#include <aoe2/runtime/guest_memory.hpp>
#include <array>
#include <limits>

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

void snapshot_ranges_cover_expanded_ram_without_overflow() {
    using namespace aoe2::game_hooks::diagnostics;
    constexpr uint32_t size = aoe2::runtime::kEeRamSize;
    std::vector<uint8_t> memory(size);
    const std::array resolvers{ResolveHdRdramOffset, ResolveRdramOffset, ResolveMcRdramOffset};
    for (auto resolve : resolvers) {
        Check(resolve(memory.data(), 0x02000000u, 1) == memory.data() + 0x02000000u,
              "upper RAM must not alias lower RAM");
        Check(resolve(memory.data(), size - 4, 4) == memory.data() + size - 4,
              "last complete word is valid");
        Check(resolve(memory.data(), size - 4, 5) == nullptr, "word beyond RAM is invalid");
        Check(resolve(memory.data(), 1, std::numeric_limits<uint32_t>::max()) == nullptr,
              "overflowing range is invalid");
        Check(resolve(nullptr, 0, 1) == nullptr, "null RAM is invalid");
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
    snapshot_ranges_cover_expanded_ram_without_overflow();
    hd_region_is_valid();
    formatting_snapshots_produces_expected_output();
    std::cout << "hd snapshot tests passed\n";
    return 0;
}
