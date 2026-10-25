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

// Reference implementation from decomp's func_0012DEB8
uint32_t ReferenceTableLookup(int32_t key, const uint8_t* ram, uint32_t table, int32_t count) {
    if (table == 0 || count <= 0) return 0;
    for (int32_t i = 0; i < count; ++i) {
        int32_t entry_key;
        std::memcpy(&entry_key, ram + ((table + i * 8) & 0x01FFFFFF), 4);
        if (entry_key == key) {
            uint32_t val;
            std::memcpy(&val, ram + ((table + i * 8 + 4) & 0x01FFFFFF), 4);
            return val;
        }
    }
    return 0;
}

// Reference accessors from decomp
int32_t ReferenceCardPort(const uint8_t* ram, uint32_t card) {
    if (card == 0) return 1;
    int32_t val;
    std::memcpy(&val, ram + (card & 0x01FFFFFF), 4);
    return val;
}

int32_t ReferenceCardSlot(const uint8_t* ram, uint32_t card) {
    if (card == 0) return 0;
    int32_t val;
    std::memcpy(&val, ram + ((card + 4) & 0x01FFFFFF), 4);
    return val;
}

int32_t ReferenceCardType(const uint8_t* ram, uint32_t card) {
    if (card == 0) return 0;
    int32_t val;
    std::memcpy(&val, ram + ((card + 8) & 0x01FFFFFF), 4);
    return val;
}

void test_differential_table_lookup() {
    MockMemory mem;
    aoe2::mc::KtMcModel model(mem);

    const uint32_t table = 0x00170000;
    const std::vector<std::pair<int32_t, uint32_t>> tuples = {
        {10, 1000}, {25, 2500}, {-5, 9999}, {0, 555}, {100, 42}
    };
    const int32_t count = static_cast<int32_t>(tuples.size());

    for (int32_t i = 0; i < count; ++i) {
        mem.Store32(table + i * 8 + 0, static_cast<uint32_t>(tuples[i].first));
        mem.Store32(table + i * 8 + 4, tuples[i].second);
    }

    for (int32_t key = -10; key <= 110; ++key) {
        const uint32_t expected = ReferenceTableLookup(key, mem.ram.data(), table, count);
        const uint32_t actual = model.TableLookup(key, table, count);
        Check(expected == actual, "differential table lookup match");
    }
}

void test_differential_accessors() {
    MockMemory mem;
    aoe2::mc::KtMcModel model(mem);

    // Null checks
    Check(model.GetCardPort(0) == ReferenceCardPort(mem.ram.data(), 0), "null port match");
    Check(model.GetCardSlot(0) == ReferenceCardSlot(mem.ram.data(), 0), "null slot match");
    Check(model.GetCardType(0) == ReferenceCardType(mem.ram.data(), 0), "null type match");

    // Non-null checks across various descriptor patterns
    const uint32_t card_addr = 0x00180000;
    for (int32_t port = 0; port < 3; ++port) {
        for (int32_t slot = 0; slot < 4; ++slot) {
            for (int32_t type = 0; type < 3; ++type) {
                mem.Store32(card_addr + 0, static_cast<uint32_t>(port));
                mem.Store32(card_addr + 4, static_cast<uint32_t>(slot));
                mem.Store32(card_addr + 8, static_cast<uint32_t>(type));

                Check(model.GetCardPort(card_addr) == ReferenceCardPort(mem.ram.data(), card_addr), "port match");
                Check(model.GetCardSlot(card_addr) == ReferenceCardSlot(mem.ram.data(), card_addr), "slot match");
                Check(model.GetCardType(card_addr) == ReferenceCardType(mem.ram.data(), card_addr), "type match");
            }
        }
    }
}
}  // namespace

int main() {
    test_differential_table_lookup();
    test_differential_accessors();
    std::cout << "mc differential tests passed\n";
    return 0;
}
