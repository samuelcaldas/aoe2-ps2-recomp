#include <aoe2/hd/hd_storage_model.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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

void test_hd_storage_read_and_exists() {
    MockMemory mem;
    aoe2::hd::HdStorageModel model(mem);

    const std::string test_dir = "/tmp/aoe2_test_hd";
    std::filesystem::create_directories(test_dir);
    const std::string file_path = test_dir + "/test.dat";
    {
        std::ofstream out(file_path, std::ios::binary);
        out.write("ABCDEFGHIJKLMN", 14);
    }

    model.Initialize(test_dir);

    // FileExists
    Check(model.FileExists("test.dat") == 1, "test.dat exists");
    Check(model.FileExists("missing.dat") == 0, "missing.dat does not exist");

    // ReadFile with offset 4, size 6 -> "EFGHIJ"
    const uint32_t dest = 0x00200000;
    const int32_t read_bytes = model.ReadFile("test.dat", dest, 6, 4);
    Check(read_bytes == 6, "read 6 bytes");

    char buf[16] = {0};
    mem.ReadBytes(dest, reinterpret_cast<uint8_t*>(buf), 6);
    Check(std::strncmp(buf, "EFGHIJ", 6) == 0, "read bytes match offset");

    std::filesystem::remove_all(test_dir);
}
}  // namespace

int main() {
    test_hd_storage_read_and_exists();
    std::cout << "hd_storage_model tests passed\n";
    return 0;
}
