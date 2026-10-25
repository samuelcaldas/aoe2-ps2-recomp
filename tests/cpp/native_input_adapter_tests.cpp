#include "../../game_hooks/src/native_input_arguments.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
}

int main() {
    std::array<uint32_t, 32> registers{};
    for (uint32_t index = 0; index < 8; ++index) registers[4 + index] = 0x100u + index;
    registers[11] = 0xFFFFFFFFu;
    const auto arguments = aoe2::game_hooks::native_input::Arguments::FromRegisters(registers);

    Check(arguments.U(0) == 0x100u, "argument 0 must read a0");
    Check(arguments.I(3) == 0x103, "argument 3 must read a3 as signed int");
    Check(arguments.U(4) == 0x104u, "argument 4 must read t0");
    Check(arguments.U(7) == 0xFFFFFFFFu, "argument 7 must read t3");
    Check(arguments.I(7) == -1, "signed arguments must preserve the guest sign bit");
    Check(arguments.U(8) == 0, "out-of-range arguments must not read unrelated registers");
    return 0;
}
