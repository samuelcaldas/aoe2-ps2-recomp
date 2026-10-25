#include <aoe2/file/kt_file_model.hpp>

#include <array>
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

    uint8_t Load8(uint32_t address) override {
        return ram[address & 0x01FFFFFF];
    }
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
    void Store8(uint32_t address, uint8_t value) override {
        ram[address & 0x01FFFFFF] = value;
    }
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

void test_binary_search() {
    MockMemory mem;
    aoe2::file::KtFileModel model(mem);

    const uint32_t table_addr = 0x00100000;
    // 3 entries: id=10, id=20, id=30, terminating entry id=0
    // Each FileRecord is 8 bytes: u16 id, u16 aux, u32 payload
    mem.Store16(table_addr + 0 * 8, 10);
    mem.Store16(table_addr + 1 * 8, 20);
    mem.Store16(table_addr + 2 * 8, 30);
    mem.Store16(table_addr + 3 * 8, 0);

    // Empty count -> not found (returns 0)
    Check(model.BinarySearch(10, table_addr, 0) == 0, "count 0 returns 0");
    // Null table -> returns 0
    Check(model.BinarySearch(10, 0, 3) == 0, "null table returns 0");

    // Exact matches
    Check(model.BinarySearch(10, table_addr, 3) == table_addr + 0 * 8, "match first entry");
    Check(model.BinarySearch(20, table_addr, 3) == table_addr + 1 * 8, "match mid entry");
    Check(model.BinarySearch(30, table_addr, 3) == table_addr + 2 * 8, "match last entry");

    // Bit 15 mask match (id | 0x8000)
    Check(model.BinarySearch(20, table_addr, 3) == table_addr + 1 * 8, "masked match");

    // Missing key
    Check(model.BinarySearch(15, table_addr, 3) == 0, "missing key returns 0");
    Check(model.BinarySearch(5, table_addr, 3) == 0, "key below range returns 0");
    Check(model.BinarySearch(35, table_addr, 3) == 0, "key above range returns 0");
}

void test_classify_kind() {
    MockMemory mem;
    aoe2::file::KtFileModel model(mem);

    // null pointer -> 3
    Check(model.ClassifyKind(0) == 3, "null classified as 3");

    // non-null pointer with cmd & 7 != 0 -> 0
    const uint32_t addr0 = 0x00100100;
    mem.Store16(addr0, 0x0001); // lower bits != 0
    Check(model.ClassifyKind(addr0) == 0, "kind 0 classification");

    // non-null pointer with cmd & 7 == 0, val == 0x40 -> 1
    const uint32_t addr1 = 0x00100108;
    mem.Store16(addr1, 0x0040);
    Check(model.ClassifyKind(addr1) == 1, "kind 1 classification");

    // non-null pointer with cmd & 7 == 0, val != 0x40 -> 2
    const uint32_t addr2 = 0x00100110;
    mem.Store16(addr2, 0x0080);
    Check(model.ClassifyKind(addr2) == 2, "kind 2 classification");
}

void test_record_and_entry_table_setup() {
    MockMemory mem;
    aoe2::file::KtFileModel model(mem);

    const uint32_t table_addr = 0x00100000;
    mem.Store16(table_addr + 0 * 8, 10);
    mem.Store32(table_addr + 0 * 8, (100 << 16) | 10); // payload for min_val calculation
    mem.Store16(table_addr + 1 * 8, 20);
    mem.Store32(table_addr + 1 * 8, (50 << 16) | 20);
    mem.Store16(table_addr + 2 * 8, 0); // sentinel

    model.SetRecordTable(table_addr);
    Check(model.RecordCount() == 2, "record count is 2");
    Check(model.RecordTableAddress() == table_addr, "record table address updated");
}

}  // namespace

int main() {
    test_binary_search();
    test_classify_kind();
    test_record_and_entry_table_setup();
    std::cout << "kt_file_model tests passed\n";
    return 0;
}
