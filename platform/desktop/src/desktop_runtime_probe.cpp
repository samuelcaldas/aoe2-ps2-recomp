#include <iostream>
#include <cstdint>

// Forward declaration of registration structure or table
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount;

int main() {
    std::cout << "=== PS2Recomp Desktop Runtime Link Probe ===" << std::endl;
    std::cout << "Probe binary loaded successfully." << std::endl;
    std::cout << "DO NOT execute guest code in this probe." << std::endl;
    std::cout << "Registered slot count: " << g_ps2RecompiledFunctionTableSlotCount << std::endl;
    return 0;
}
