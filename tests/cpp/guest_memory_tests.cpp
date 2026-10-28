#include <aoe2/runtime/guest_memory.hpp>

#include <cstdlib>
#include <iostream>
#include <limits>

namespace {
void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}
}

int main() {
    aoe2::runtime::GuestMemory memory;
    Check(memory.Initialize(), "initialize guest RAM");
    constexpr uint32_t size = aoe2::runtime::kEeRamSize;
    Check(size == 64u * 1024u * 1024u, "64 MiB guest RAM");
    memory.Write32(0, 0x12345678);
    memory.Write32(0x02000000u, 0x87654321);
    Check(memory.Read32(0) == 0x12345678, "upper RAM does not alias lower RAM");
    Check(memory.Read32(0x02000000u) == 0x87654321, "upper RAM is readable");
    memory.Write32(size - 4, 0xabcdef01);
    Check(memory.Read32(size - 4) == 0xabcdef01, "last word is accessible");
    memory.Write32(size - 3, 0);
    memory.Write16(size - 1, 0);
    Check(memory.Read32(size - 4) == 0xabcdef01, "cross-boundary writes are rejected");
    Check(memory.Read16(size - 1) == 0, "cross-boundary halfword reads are rejected");
    Check(memory.Read32(size - 3) == 0, "cross-boundary word reads are rejected");
    constexpr auto invalid = std::numeric_limits<uint32_t>::max();
    Check(memory.Read16(invalid) == 0, "overflowing halfword reads are rejected");
    Check(memory.Read32(invalid) == 0, "overflowing word reads are rejected");
    memory.Write16(invalid, 0);
    memory.Write32(invalid, 0);
    Check(memory.Read32(0) == 0x12345678, "overflowing writes preserve lower RAM");
    Check(memory.GetPointer(size) == nullptr, "pointer at exclusive end is rejected");
    memory.Reset();
    Check(memory.Read32(0x02000000u) == 0, "reset clears upper RAM");
    std::cout << "guest memory tests passed\n";
}
