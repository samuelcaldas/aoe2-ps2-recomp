#include "state_snapshot.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

using namespace aoe2::game_hooks::diagnostics;

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

void regions_do_not_overlap_and_match_the_documented_areas() {
    Check(kMouseRegion.address + kMouseRegion.size <= kKeyboardRegion.address, "mouse area ends before the keyboard area");
    Check(kMouseRegion.address == 0x509F00 && kMouseRegion.size == 0x3C0, "mouse area");
    Check(kKeyboardRegion.address == 0x50A2C0 && kKeyboardRegion.size == 0x280, "keyboard area");
    Check(kKeyboardRegion.address + kKeyboardRegion.size <= 0x50A540, "keyboard area ends before the USB driver data");
}

void schedule_logs_the_first_16_then_every_64th() {
    Check(!ShouldSnapshot(0), "0 is not a snapshot");
    for (uint64_t n = 1; n <= 16; ++n) Check(ShouldSnapshot(n), "first 16 are logged");
    Check(!ShouldSnapshot(17) && !ShouldSnapshot(63), "sparse afterwards");
    Check(ShouldSnapshot(64) && ShouldSnapshot(128), "every 64th");
}

void hash_and_format_are_stable() {
    const std::array<uint8_t, 4> bytes{0x00, 0x01, 0xAB, 0xFF};
    Check(Fnv1a(bytes.data(), 0) == 14695981039346656037ull, "empty input hashes to the FNV offset");
    Check(Fnv1a(bytes.data(), bytes.size()) == Fnv1a(bytes.data(), bytes.size()), "hash is deterministic");
    const StateRegion tiny{"tiny", 0x1000, 4};
    const std::string line = FormatSnapshot(tiny, 3, 7, bytes.data());
    Check(line.find("label=tiny n=3 steps=7 fnv=") == 0 || line.find("[state-snapshot] label=tiny n=3 steps=7 fnv=") == 0, "line header");
    Check(line.find(" bytes=0001abff") != std::string::npos, "bytes are lowercase hex without separators");
    Check(FormatSkip(tiny, 2, "invalid-range") == "[state-snapshot-skip] label=tiny n=2 reason=invalid-range", "skip line");
    Check(FormatConfig(false) == "[state-snapshot-config] enabled=0" && FormatConfig(true) == "[state-snapshot-config] enabled=1", "config line");
}
}  // namespace

int main() {
    regions_do_not_overlap_and_match_the_documented_areas();
    schedule_logs_the_first_16_then_every_64th();
    hash_and_format_are_stable();
    std::cout << "state snapshot tests passed\n";
    return 0;
}
