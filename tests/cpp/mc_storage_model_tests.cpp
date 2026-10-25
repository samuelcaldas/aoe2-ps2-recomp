#include <aoe2/mc/mc_storage_model.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
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

void test_storage_write_read_delete() {
    MockMemory mem;
    aoe2::mc::McStorageModel model(mem);

    const std::string test_dir = "/tmp/aoe2_test_mc";
    std::filesystem::create_directories(test_dir + "/mc0");
    std::filesystem::create_directories(test_dir + "/mc1");

    model.Initialize(test_dir);

    // Prepare test payload in guest RAM
    const uint32_t src_addr = 0x00200000;
    const char payload[] = "AOE2_SAVE_GAME_DATA_TEST";
    const uint32_t payload_len = sizeof(payload);
    mem.WriteBytes(src_addr, reinterpret_cast<const uint8_t*>(payload), payload_len);

    // Write file to card 0
    const int32_t written = model.WriteFile(0, 0, "SAVE001.DAT", src_addr, payload_len);
    Check(written == static_cast<int32_t>(payload_len), "write file returns bytes written");

    // Read file from card 0 into another guest address
    const uint32_t dest_addr = 0x00210000;
    const int32_t read_bytes = model.ReadFile(0, 0, "SAVE001.DAT", dest_addr);
    Check(read_bytes == static_cast<int32_t>(payload_len), "read file returns bytes read");

    char dest_buf[64] = {0};
    mem.ReadBytes(dest_addr, reinterpret_cast<uint8_t*>(dest_buf), payload_len);
    Check(std::strcmp(dest_buf, payload) == 0, "payload matches written data");

    // GetDir should list the file
    const uint32_t entries_addr = 0x00220000;
    const int32_t count = model.GetDir(0, 0, "*", entries_addr, 10);
    Check(count >= 1, "getdir finds at least 1 file");

    // Delete file
    const int32_t deleted = model.DeleteFile(0, 0, "SAVE001.DAT");
    Check(deleted == 0, "delete file succeeds");

    // Cleanup host test directory
    std::filesystem::remove_all(test_dir);
}
}  // namespace

int main() {
    test_storage_write_read_delete();
    std::cout << "mc_storage_model tests passed\n";
    return 0;
}
