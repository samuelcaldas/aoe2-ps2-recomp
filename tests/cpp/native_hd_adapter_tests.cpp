#include <aoe2/hd/kt_hd_model.hpp>
#include "../../game_hooks/src/native_input_arguments.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

class MockMemory final : public aoe2::runtime::GuestAccess {
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(32 * 1024 * 1024, 0);

    uint8_t Load8(uint32_t address) override { return ram[address & 0x01FFFFFF]; }
    uint16_t Load16(uint32_t address) override {
        uint16_t val;
        std::memcpy(&val, &ram[address & 0x01FFFFFF], 2);
        return val;
    }
    uint32_t Load32(uint32_t address) override {
        uint32_t val;
        std::memcpy(&val, &ram[address & 0x01FFFFFF], 4);
        return val;
    }
    void Store8(uint32_t address, uint8_t value) override { ram[address & 0x01FFFFFF] = value; }
    void Store16(uint32_t address, uint16_t value) override {
        std::memcpy(&ram[address & 0x01FFFFFF], &value, 2);
    }
    void Store32(uint32_t address, uint32_t value) override {
        std::memcpy(&ram[address & 0x01FFFFFF], &value, 4);
    }
    void ReadBytes(uint32_t address, uint8_t* out, std::size_t count) override {
        std::memcpy(out, &ram[address & 0x01FFFFFF], count);
    }
    void WriteBytes(uint32_t address, const uint8_t* in, std::size_t count) override {
        std::memcpy(&ram[address & 0x01FFFFFF], in, count);
    }
    void ReportAssertion(std::string_view, int, std::string_view) override {}
};

void test_hd_adapter_binding() {
    MockMemory mem;
    aoe2::hd::KtHdModel hd_model(mem);

    const uint32_t table_addr = 0x00100000;
    mem.Store32(table_addr + 0 * 8 + 0, 77);
    mem.Store32(table_addr + 0 * 8 + 4, 888);

    std::array<uint32_t, 32> registers{};
    registers[4] = table_addr; // a0: table
    registers[5] = 1;          // a1: count
    registers[6] = 77;         // a2: key
    const auto arguments = aoe2::game_hooks::native_input::Arguments::FromRegisters(registers);

    const int32_t result = hd_model.TableLookup(arguments.U(0), arguments.I(1), arguments.I(2));
    Check(result == 888, "TableLookup via register arguments returns mapped value");
}
}  // namespace

int main() {
    test_hd_adapter_binding();
    std::cout << "native_hd_adapter tests passed\n";
    return 0;
}
