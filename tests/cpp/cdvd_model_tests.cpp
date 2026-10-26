#include <aoe2/cdvd/cdvd_model.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#if AOE2_TEST_NATIVE_CDVD
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#endif

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
#if AOE2_TEST_NATIVE_CDVD
void original_scmd_semaphore(uint8_t* ram, R5900Context* context, PS2Runtime*) {
    uint32_t id = 0;
    std::memcpy(&id, ram + 0x4ED34C, sizeof(id));
    setReturnU32(context, id);
    context->pc = getRegU32(context, 31);
}

void install_cdvd(PS2Runtime& runtime, const char* switches) {
    const auto saved_paths = PS2Runtime::getIoPaths();
    auto paths = saved_paths;
    paths.elfDirectory = std::filesystem::temp_directory_path() /
        ("aoe2-cdvd-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Check(std::filesystem::create_directory(paths.elfDirectory), "create test config directory");
    std::ofstream(paths.elfDirectory / "native_modules.ini") << switches;
    PS2Runtime::setIoPaths(paths);
    ps2_game_overrides::applyMatching(runtime, "AOK.ELF", 0, 0, false);
    PS2Runtime::setIoPaths(saved_paths);
    std::filesystem::remove_all(paths.elfDirectory);
}

void exercise_scmd_hook(uint8_t*, R5900Context* guest, PS2Runtime* host) {
    auto& runtime = *host;
    const bool enabled = getRegU32(guest, 4) != 0;
    auto& scheduler = runtime.eeScheduler();
    const int id = scheduler.createSemaphore(0, 1, 0, 0);
    Check(id > 0, "SCmd semaphore creation");
    auto* ram = runtime.memory().getRDRAM();
    std::memcpy(ram + 0x4ED34C, &id, sizeof(id));
    R5900Context context{};
    SET_GPR_U32(&context, 31, 0x123456);
    for (int command = 0; command < 2; ++command) {
        runtime.lookupFunction(0x0012BC20)(ram, &context, &runtime);
        Check(getRegU32(&context, 2) == static_cast<uint32_t>(id), "SCmd hook preserves semaphore ID");
        Check(context.pc == 0x123456, "SCmd hook returns to caller");
        Check(scheduler.semaphore(id)->count == (enabled ? 1 : 0), "SCmd completion signal count");
        if (!enabled) continue;
        scheduler.waitSemaphore(id);
        Check(scheduler.semaphore(id)->count == 0, "WaitSema consumes SCmd completion");
        Check(scheduler.semaphore(id)->waiters.empty(), "WaitSema must not block after completion");
    }
    scheduler.requestStop();
}

void test_scmd_semaphore_completion(const char* switches, bool enabled) {
    PS2Runtime runtime;
    Check(runtime.memory().initialize(), "runtime memory initialization");
    Check(runtime.syncCoreSubsystems(), "runtime scheduler initialization");
    runtime.registerFunction(0x0012BC20, original_scmd_semaphore);
    install_cdvd(runtime, switches);
    runtime.registerFunction(0x1000, exercise_scmd_hook);
    R5900Context main_context{};
    main_context.pc = 0x1000;
    SET_GPR_U32(&main_context, 4, enabled ? 1 : 0);
    runtime.eeScheduler().reset(runtime.memory().getRDRAM(), main_context);
    runtime.eeScheduler().run();
}
#endif
}  // namespace

int main() {
    test_cdvd_initialization_and_status();
    test_cdvd_read_sectors_and_streaming();
#if AOE2_TEST_NATIVE_CDVD
    test_scmd_semaphore_completion("cdvd=off\nfile_cd=off\n", false);
    test_scmd_semaphore_completion("cdvd=on\n", true);
    test_scmd_semaphore_completion("file_cd=on\n", true);
#endif
    std::cout << "cdvd_model tests passed\n";
    return 0;
}
