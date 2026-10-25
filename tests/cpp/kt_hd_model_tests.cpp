#include <aoe2/hd/kt_hd_model.hpp>

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

void test_table_lookup() {
    MockMemory mem;
    aoe2::hd::KtHdModel model(mem);

    const uint32_t table = 0x00100000;
    // 3 entries: (10 -> 100), (20 -> 200), (30 -> 300)
    mem.Store32(table + 0 * 8 + 0, 10);
    mem.Store32(table + 0 * 8 + 4, 100);
    mem.Store32(table + 1 * 8 + 0, 20);
    mem.Store32(table + 1 * 8 + 4, 200);
    mem.Store32(table + 2 * 8 + 0, 30);
    mem.Store32(table + 2 * 8 + 4, 300);

    Check(model.TableLookup(table, 3, 20) == 200, "lookup 20");
    Check(model.TableLookup(table, 3, 10) == 100, "lookup 10");
    Check(model.TableLookup(table, 3, 30) == 300, "lookup 30");
    Check(model.TableLookup(table, 3, 999) == 0, "lookup missing");
    Check(model.TableLookup(table, 0, 10) == 0, "lookup count 0");
    Check(model.TableLookup(table, -1, 10) == 0, "lookup negative count");
}

void test_flags_and_registers() {
    MockMemory mem;
    aoe2::hd::KtHdModel model(mem);

    // Flag setting and getting
    Check(model.SetFlag(1) == 1, "set flag 1");
    Check(model.GetFlag() == 1, "get flag 1");
    Check(model.SetFlag(0) == 0, "set flag 0");
    Check(model.GetFlag() == 0, "get flag 0");
    Check(model.SetFlag(5) == 0, "invalid flag ignored");

    // Register read and clear (D_4ED300[0x16] at guest 0x004ED300 + 0x16 * 4)
    constexpr uint32_t kRegAddr = 0x004ED300 + 0x16 * 4;
    mem.Store32(kRegAddr, 0x12345678);

    Check(model.ReadAndClearRegister() == 0x12345678, "reads register value");
    Check(mem.Load32(kRegAddr) == 0, "register cleared after read");
    Check(model.ReadAndClearRegister() == 0, "subsequent read returns 0");
}
}  // namespace

int main() {
    test_table_lookup();
    test_flags_and_registers();
    std::cout << "kt_hd_model tests passed\n";
    return 0;
}
