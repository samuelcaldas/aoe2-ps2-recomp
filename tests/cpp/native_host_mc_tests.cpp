#include <aoe2/mc/native_host_mc.hpp>

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <vector>

namespace {

void Check(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "FAIL: " << msg << std::endl;
        std::exit(1);
    }
}

class TestMemory final : public aoe2::runtime::GuestAccess {
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(4 * 1024 * 1024, 0);

    uint8_t Load8(uint32_t address) override { return ram[address & 0x003FFFFF]; }
    uint16_t Load16(uint32_t address) override {
        uint16_t val;
        std::memcpy(&val, &ram[address & 0x003FFFFF], 2);
        return val;
    }
    uint32_t Load32(uint32_t address) override {
        uint32_t val;
        std::memcpy(&val, &ram[address & 0x003FFFFF], 4);
        return val;
    }
    void Store8(uint32_t address, uint8_t value) override { ram[address & 0x003FFFFF] = value; }
    void Store16(uint32_t address, uint16_t value) override {
        std::memcpy(&ram[address & 0x003FFFFF], &value, 2);
    }
    void Store32(uint32_t address, uint32_t value) override {
        std::memcpy(&ram[address & 0x003FFFFF], &value, 4);
    }
    void ReadBytes(uint32_t address, uint8_t* out, std::size_t count) override {
        std::memcpy(out, &ram[address & 0x003FFFFF], count);
    }
    void WriteBytes(uint32_t address, const uint8_t* in, std::size_t count) override {
        std::memcpy(&ram[address & 0x003FFFFF], in, count);
    }
    void ReportAssertion(std::string_view, int, std::string_view) override {}
};

void TestSaveAndLoadLifecycle() {
    auto& mc = aoe2::mc::NativeHostMemoryCard::Instance();
    mc.Reset();

    const std::filesystem::path test_dir = std::filesystem::temp_directory_path() / "aoe2_test_mc0";
    std::filesystem::remove_all(test_dir);
    mc.SetRootDirectory(0, test_dir);

    TestMemory mem;

    // 1. GetInfo
    uint32_t type_ptr = 0x1000;
    uint32_t free_ptr = 0x1004;
    uint32_t fmt_ptr = 0x1008;
    mc.SceMcGetInfo(0, 0, type_ptr, free_ptr, fmt_ptr, mem);

    uint32_t cmd_ptr = 0x1010;
    uint32_t res_ptr = 0x1014;
    int32_t sync_res = mc.SceMcSync(0, cmd_ptr, res_ptr, mem);
    Check(sync_res == 1, "SceMcSync returns 1 for pending op");
    Check(mem.Load32(cmd_ptr) == aoe2::mc::kMcCmdGetInfo, "cmd is GetInfo");
    Check(mem.Load32(res_ptr) == 1, "result is formatted (1)");
    Check(mem.Load32(type_ptr) == aoe2::mc::kMcTypePs2, "type is PS2");

    // 2. Mkdir BISLPM-65000
    mc.SceMcMkdir(0, 0, "BISLPM-65000");
    sync_res = mc.SceMcSync(0, cmd_ptr, res_ptr, mem);
    Check(mem.Load32(cmd_ptr) == aoe2::mc::kMcCmdMkdir, "cmd is Mkdir");
    Check(mem.Load32(res_ptr) == aoe2::mc::kMcResultSucceed, "mkdir succeed");
    Check(std::filesystem::is_directory(test_dir / "BISLPM-65000"), "host folder created");

    // 3. Open file with create
    // PS2_FIO_O_CREAT (0x0200) | PS2_FIO_O_WRONLY (0x0002)
    mc.SceMcOpen(0, 0, "BISLPM-65000/savedgame.dat", 0x0202, mem);
    sync_res = mc.SceMcSync(0, cmd_ptr, res_ptr, mem);
    Check(mem.Load32(cmd_ptr) == aoe2::mc::kMcCmdOpen, "cmd is Open");
    int32_t fd = static_cast<int32_t>(mem.Load32(res_ptr));
    Check(fd > 0, "open returned valid fd");

    // 4. Write data
    const char payload[] = "AGE OF EMPIRES II PS2 SAVE DATA TEST 123456789";
    const uint32_t guest_buf = 0x2000;
    mem.WriteBytes(guest_buf, reinterpret_cast<const uint8_t*>(payload), sizeof(payload));

    mc.SceMcWrite(fd, guest_buf, sizeof(payload), mem);
    sync_res = mc.SceMcSync(0, cmd_ptr, res_ptr, mem);
    Check(mem.Load32(cmd_ptr) == aoe2::mc::kMcCmdWrite, "cmd is Write");
    Check(mem.Load32(res_ptr) == sizeof(payload), "bytes written matches payload");

    // 5. Close
    mc.SceMcClose(fd);
    sync_res = mc.SceMcSync(0, cmd_ptr, res_ptr, mem);
    Check(mem.Load32(cmd_ptr) == aoe2::mc::kMcCmdClose, "cmd is Close");
    Check(mem.Load32(res_ptr) == aoe2::mc::kMcResultSucceed, "close succeed");

    // Verify host file exists and has content
    const auto host_file = test_dir / "BISLPM-65000/savedgame.dat";
    Check(std::filesystem::exists(host_file), "host file exists");
    Check(std::filesystem::file_size(host_file) == sizeof(payload), "host file size matches");

    // 6. GetDir listing
    const uint32_t table_addr = 0x3000;
    mc.SceMcGetDir(0, 0, "BISLPM-65000/*", 0, 10, table_addr, mem);
    sync_res = mc.SceMcSync(0, cmd_ptr, res_ptr, mem);
    Check(mem.Load32(cmd_ptr) == aoe2::mc::kMcCmdGetDir, "cmd is GetDir");
    Check(mem.Load32(res_ptr) == 1, "found 1 file");

    aoe2::mc::SceMcTblGetDir row{};
    mem.ReadBytes(table_addr, reinterpret_cast<uint8_t*>(&row), sizeof(row));
    Check(std::strcmp(row.EntryName, "savedgame.dat") == 0, "entry name matches");
    Check(row.FileSizeByte == sizeof(payload), "entry size matches");
    Check((row.AttrFile & aoe2::mc::kMcAttrFile) != 0, "is file attribute");

    // 7. Read back file
    mc.SceMcOpen(0, 0, "BISLPM-65000/savedgame.dat", 0x0001, mem); // PS2_FIO_O_RDONLY
    mc.SceMcSync(0, cmd_ptr, res_ptr, mem);
    int32_t read_fd = static_cast<int32_t>(mem.Load32(res_ptr));
    Check(read_fd > 0, "opened for read");

    const uint32_t dest_buf = 0x4000;
    mc.SceMcRead(read_fd, dest_buf, sizeof(payload), mem);
    mc.SceMcSync(0, cmd_ptr, res_ptr, mem);
    Check(mem.Load32(res_ptr) == sizeof(payload), "read full payload");

    char read_payload[sizeof(payload)]{};
    mem.ReadBytes(dest_buf, reinterpret_cast<uint8_t*>(read_payload), sizeof(payload));
    Check(std::memcmp(read_payload, payload, sizeof(payload)) == 0, "data read back matches written data");

    mc.SceMcClose(read_fd);
    mc.SceMcSync(0, cmd_ptr, res_ptr, mem);

    // Cleanup
    std::filesystem::remove_all(test_dir);
}

}  // namespace

int main() {
    TestSaveAndLoadLifecycle();
    std::cout << "NativeHostMemoryCard tests passed successfully!\n";
    return 0;
}
