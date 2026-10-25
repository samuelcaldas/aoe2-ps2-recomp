#include <aoe2/mc/kt_mc_model.hpp>

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
    int assert_calls = 0;

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
    void ReportAssertion(std::string_view, int, std::string_view) override {
        ++assert_calls;
    }
};

void test_table_lookup() {
    MockMemory mem;
    aoe2::mc::KtMcModel model(mem);

    const uint32_t table_addr = 0x00100000;
    // 2 entries: (10 -> 100), (20 -> 200)
    mem.Store32(table_addr + 0 * 8 + 0, 10);
    mem.Store32(table_addr + 0 * 8 + 4, 100);
    mem.Store32(table_addr + 1 * 8 + 0, 20);
    mem.Store32(table_addr + 1 * 8 + 4, 200);

    Check(model.TableLookup(10, table_addr, 2) == 100, "lookup entry 1");
    Check(model.TableLookup(20, table_addr, 2) == 200, "lookup entry 2");
    Check(model.TableLookup(30, table_addr, 2) == 0, "lookup missing returns 0");
}

void test_card_limit_validation() {
    MockMemory mem;
    aoe2::mc::KtMcModel model(mem);

    model.Init2(0x00100000, 5);
    Check(mem.assert_calls > 0, "n_card > 2 triggers assertion");
}

void test_accessors_and_setters() {
    MockMemory mem;
    aoe2::mc::KtMcModel model(mem);

    const uint32_t card = 0x00100100;
    mem.Store32(card + 0, 111); // port
    mem.Store32(card + 4, 222); // slot
    mem.Store32(card + 8, 333); // type

    // Non-null descriptor reads
    Check(model.GetCardPort(card) == 111, "card port");
    Check(model.GetCardSlot(card) == 222, "card slot");
    Check(model.GetCardType(card) == 333, "card type");

    // NULL descriptor defaults
    Check(model.GetCardPort(0) == 1, "NULL port default is 1");
    Check(model.GetCardSlot(0) == 0, "NULL slot default is 0");
    Check(model.GetCardType(0) == 0, "NULL type default is 0");

    // Field setters at +0x18 and +0x1C
    const uint32_t file_ptr = 0x00100200;
    model.SetFileAttr(file_ptr, 9999);
    Check(mem.Load32(file_ptr + 0x18) == 9999, "attr written at +0x18");

    model.SetFileCluster(file_ptr, 8888);
    Check(mem.Load32(file_ptr + 0x1C) == 8888, "cluster written at +0x1C");
}
}  // namespace

int main() {
    test_table_lookup();
    test_card_limit_validation();
    test_accessors_and_setters();
    std::cout << "kt_mc_model tests passed\n";
    return 0;
}
