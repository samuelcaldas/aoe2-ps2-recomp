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

// Reference implementation from decomp's func_00129588
int32_t ReferenceTableLookup(const uint8_t* ram, uint32_t table, int32_t count, int32_t key) {
    if (table == 0 || count <= 0) return 0;
    for (int32_t i = 0; i < count; ++i) {
        int32_t entry_key;
        std::memcpy(&entry_key, ram + ((table + i * 8) & 0x01FFFFFF), 4);
        if (entry_key == key) {
            int32_t val;
            std::memcpy(&val, ram + ((table + i * 8 + 4) & 0x01FFFFFF), 4);
            return val;
        }
    }
    return 0;
}

void test_differential_table_lookup() {
    MockMemory mem;
    aoe2::hd::KtHdModel model(mem);

    const uint32_t table = 0x00190000;
    const std::vector<std::pair<int32_t, int32_t>> entries = {
        {10, 100}, {20, 200}, {30, 300}, {40, 400}, {50, 500}
    };
    const int32_t count = static_cast<int32_t>(entries.size());

    for (int32_t i = 0; i < count; ++i) {
        mem.Store32(table + i * 8 + 0, static_cast<uint32_t>(entries[i].first));
        mem.Store32(table + i * 8 + 4, static_cast<uint32_t>(entries[i].second));
    }

    for (int32_t key = 0; key <= 60; ++key) {
        const int32_t expected = ReferenceTableLookup(mem.ram.data(), table, count, key);
        const int32_t actual = model.TableLookup(table, count, key);
        Check(expected == actual, "differential table lookup match");
    }
}

void test_differential_flag_transitions() {
    MockMemory mem;
    aoe2::hd::KtHdModel model(mem);

    constexpr uint32_t kFlagAddr = 0x004EA180;
    mem.Store32(kFlagAddr, 0);

    const std::vector<int32_t> test_inputs = {1, 1, 0, 5, -1, 1, 0, 0, 2};
    for (int32_t input : test_inputs) {
        // Expected behavior according to decomp func_00128F40:
        // if flag == 0 or 1, store flag; return current flag
        uint32_t cur = mem.Load32(kFlagAddr);
        if (input == 0 || input == 1) cur = input;

        const int32_t actual = model.SetFlag(input);
        Check(actual == static_cast<int32_t>(cur), "flag return matches");
        Check(model.GetFlag() == static_cast<int32_t>(cur), "get flag matches");
    }
}
}  // namespace

int main() {
    test_differential_table_lookup();
    test_differential_flag_transitions();
    std::cout << "hd differential tests passed\n";
    return 0;
}
