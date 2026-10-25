#include <aoe2/cdvd/cdvd_model.hpp>

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

void test_cdvd_initialization_and_status() {
    MockMemory mem;
    aoe2::cdvd::CdvdModel model(mem);

    Check(model.Reset() == 0, "reset succeeds");
    Check(!model.IsActive(), "stream not active initially");
    Check(model.RemainingBytes() == 0, "0 bytes remaining initially");
}

void test_cdvd_read_sectors_and_streaming() {
    MockMemory mem;
    aoe2::cdvd::CdvdModel model(mem);

    const uint32_t dest = 0x00200000;
    // Queue read of 2 sectors (4096 bytes)
    const int32_t res = model.QueueRead(100, 2, dest);
    Check(res == 0, "queue read succeeds");
    Check(model.IsActive(), "stream active after queue read");
    Check(model.RemainingBytes() == 4096, "4096 bytes remaining");
    Check(model.RemainingSectors() == 2, "2 sectors remaining");

    // Process requests
    model.ProcessPendingRequests();
    Check(!model.IsActive(), "stream inactive after completion");
    Check(model.RemainingBytes() == 0, "0 bytes remaining after completion");
}
}  // namespace

int main() {
    test_cdvd_initialization_and_status();
    test_cdvd_read_sectors_and_streaming();
    std::cout << "cdvd_model tests passed\n";
    return 0;
}
