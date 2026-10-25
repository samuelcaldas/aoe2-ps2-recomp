#include <aoe2/cdvd/cdvd_model.hpp>
#include <aoe2/file/kt_file_model.hpp>
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

void test_file_adapter_binding() {
    MockMemory mem;
    aoe2::file::KtFileModel file_model(mem);

    // Test SetRecordTable via argument unpack
    const uint32_t table_addr = 0x00100000;
    mem.Store16(table_addr, 0); // empty table

    std::array<uint32_t, 32> registers{};
    registers[4] = table_addr; // a0
    const auto arguments = aoe2::game_hooks::native_input::Arguments::FromRegisters(registers);

    const uint32_t old_table = file_model.SetRecordTable(arguments.U(0));
    Check(old_table == 0, "initial old table is 0");
    Check(file_model.RecordTableAddress() == table_addr, "table address set");
}

void test_cdvd_adapter_binding() {
    MockMemory mem;
    aoe2::cdvd::CdvdModel cdvd_model(mem);

    std::array<uint32_t, 32> registers{};
    registers[4] = 100;        // a0: lba
    registers[5] = 4;          // a1: sector count
    registers[6] = 0x00200000; // a2: dest
    const auto arguments = aoe2::game_hooks::native_input::Arguments::FromRegisters(registers);

    const int32_t res = cdvd_model.QueueRead(arguments.U(0), arguments.U(1), arguments.U(2));
    Check(res == 0, "queue read succeeds");
    Check(cdvd_model.RemainingSectors() == 4, "4 sectors remaining");
    Check(cdvd_model.RemainingBytes() == 4 * 2048, "8192 bytes remaining");
}
}  // namespace

int main() {
    test_file_adapter_binding();
    test_cdvd_adapter_binding();
    std::cout << "native_file_adapter tests passed\n";
    return 0;
}
