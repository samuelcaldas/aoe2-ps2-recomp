#include "file_snapshot.hpp"

#include <array>
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

void file_and_cdvd_regions_are_valid() {
    using namespace aoe2::game_hooks::diagnostics;
    Check(kFileRegion.address == 0x004F1508, "file region starts at D_4F1508");
    Check(kFileRegion.size >= 20, "file region covers CdServerState");
    Check(kCdvdRegion.address == 0x004F16C0, "cdvd region starts at D_4F16C0");
    Check(kCdvdRegion.size >= 12, "cdvd region covers CdStreamStatus");
}

void formatting_snapshots_produces_expected_output() {
    using namespace aoe2::game_hooks::diagnostics;
    std::vector<uint8_t> rdram(32 * 1024 * 1024, 0);
    rdram[0x004F1508] = 0x12;
    rdram[0x004F1508 + 1] = 0x34;
    rdram[0x004F16C0] = 0x56;

    const std::string file_snap = FormatFileSnapshot(rdram.data(), 1, 1);
    Check(file_snap.find("label=file") != std::string::npos, "file label present");
    Check(file_snap.find("n=1") != std::string::npos, "step n=1 present");

    const std::string cdvd_snap = FormatCdvdSnapshot(rdram.data(), 1, 1);
    Check(cdvd_snap.find("label=cdvd") != std::string::npos, "cdvd label present");
    Check(cdvd_snap.find("n=1") != std::string::npos, "step n=1 present");
}
}  // namespace

int main() {
    file_and_cdvd_regions_are_valid();
    formatting_snapshots_produces_expected_output();
    std::cout << "file snapshot tests passed\n";
    return 0;
}
