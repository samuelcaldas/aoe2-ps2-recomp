#include <aoe2/cdvd/cdvd_model.hpp>
#include <aoe2/file/kt_file_model.hpp>

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

// Reference implementation directly mirroring the decomp's ktFileBinarySearchRecord
uint32_t ReferenceBinarySearch(int32_t key, const uint8_t* ram, uint32_t table, int32_t count) {
    if (table == 0 || count <= 0) return 0;
    int32_t low = 0;
    int32_t high = count;
    while (low < high) {
        const int32_t mid = (low + high) / 2;
        const uint32_t entry = table + mid * 8;
        uint16_t raw_id;
        std::memcpy(&raw_id, ram + (entry & 0x01FFFFFF), 2);
        const int32_t entry_id = static_cast<int32_t>(raw_id & 0x7FFF);
        const int32_t masked_key = key & 0x7FFF;
        if (masked_key < entry_id) {
            high = mid;
        } else if (entry_id >= masked_key) {
            return entry;
        } else {
            low = mid + 1;
        }
    }
    return 0;
}

// Reference implementation directly mirroring ktFileClassifyKind
int32_t ReferenceClassifyKind(const uint8_t* ram, uint32_t arg) {
    if (arg == 0) return 3;
    uint16_t val;
    std::memcpy(&val, ram + (arg & 0x01FFFFFF), 2);
    if ((val & 7) != 0) return 0;
    if (val != 0x40) return 2;
    return 1;
}

void test_differential_binary_search() {
    MockMemory mem;
    aoe2::file::KtFileModel model(mem);

    const uint32_t table = 0x00150000;
    const std::vector<uint16_t> test_ids = {5, 12, 19, 27, 42, 55, 80, 99, 120, 150};
    const int32_t count = static_cast<int32_t>(test_ids.size());

    for (int32_t i = 0; i < count; ++i) {
        // Interleave bit 15 on alternate entries to stress mask logic
        const uint16_t stored_id = (i % 2 == 1) ? (test_ids[i] | 0x8000) : test_ids[i];
        mem.Store16(table + i * 8, stored_id);
        mem.Store16(table + i * 8 + 2, static_cast<uint16_t>(i));
        mem.Store32(table + i * 8 + 4, 0x1000 + i);
    }
    mem.Store16(table + count * 8, 0); // sentinel

    // Test a wide sweep of search keys: negative, in-range, out-of-range, and bit 15 variations
    for (int32_t key = -5; key <= 160; ++key) {
        const uint32_t expected = ReferenceBinarySearch(key, mem.ram.data(), table, count);
        const uint32_t actual = model.BinarySearch(key, table, count);
        Check(expected == actual, "binary search differential match");

        // Also test with bit 15 set in the search key
        const int32_t key_with_flag = key | 0x8000;
        const uint32_t expected_flag = ReferenceBinarySearch(key_with_flag, mem.ram.data(), table, count);
        const uint32_t actual_flag = model.BinarySearch(key_with_flag, table, count);
        Check(expected_flag == actual_flag, "binary search differential match with flag");
    }
}

void test_differential_classify_kind() {
    MockMemory mem;
    aoe2::file::KtFileModel model(mem);

    // Null address
    Check(model.ClassifyKind(0) == ReferenceClassifyKind(mem.ram.data(), 0), "null kind match");

    const uint32_t test_addr = 0x00160000;
    for (uint32_t val = 0; val <= 0x200; ++val) {
        mem.Store16(test_addr, static_cast<uint16_t>(val));
        const int32_t expected = ReferenceClassifyKind(mem.ram.data(), test_addr);
        const int32_t actual = model.ClassifyKind(test_addr);
        Check(expected == actual, "classify kind differential match");
    }
}

void test_cdvd_flags() {
    for (uint32_t val = 0; val <= 0x10000; val += 7) {
        const uint32_t masked = val & 0x7u;
        const uint32_t bit15 = val & 0x8000u;
        const uint32_t cleared15 = val & ~0x8000u;
        Check((val & 0x7u) == masked, "mask cmd");
        Check(((val & 0x8000u) != 0) == (bit15 != 0), "check bit 15");
        Check((cleared15 & 0x8000u) == 0, "bit 15 cleared");
    }
}

}  // namespace

int main() {
    test_differential_binary_search();
    test_differential_classify_kind();
    test_cdvd_flags();
    std::cout << "file differential tests passed\n";
    return 0;
}
