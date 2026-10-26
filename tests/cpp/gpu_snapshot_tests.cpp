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

void snapshots_capture_packet() {
    using namespace aoe2::game_hooks::diagnostics;
    std::vector<uint8_t> ram(32 * 1024 * 1024);
    const uint32_t words[]{0x4030, 0, 0x4000, 0x4010};
    std::memcpy(ram.data() + 0x2000, words, 16);
    auto first = FormatGpuSnapshot(ram.data(), 0x2000, 0x125808, 1);
    ram[0x402f] = 0xff;
    auto second = FormatGpuSnapshot(ram.data(), 0x2000, 0x125808, 1);
    Check(first != second, "packet mutation changes snapshot");
    ram[0x38c740] = 42;
    Check(second == FormatGpuSnapshot(ram.data(), 0x2000, 0x125808, 1), "constant table is excluded");
    ram[0x2004] = 3;
    Check(second != FormatGpuSnapshot(ram.data(), 0x2000, 0x125808, 1), "context mutation changes snapshot");
    Check(first.find("label=gpu") != std::string::npos, "GPU label");
    Check(FormatGpuSnapshot(nullptr, 0x2000, 0, 1).find("invalid-context") != std::string::npos,
          "bad context skips");
}
}
int main() {
    snapshots_capture_packet();
    std::cout << "gpu snapshot tests passed\n";
}
