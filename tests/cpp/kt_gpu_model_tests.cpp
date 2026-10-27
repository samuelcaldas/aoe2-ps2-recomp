#include <aoe2/gpu/kt_gpu_model.hpp>

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

void test_packets() {
    using aoe2::gpu::KtGpuModel;
    using Method = void (KtGpuModel::*)(uint32_t, uint32_t);
    const Method methods[]{&KtGpuModel::SetFrame1, &KtGpuModel::SetFrame2,
        &KtGpuModel::SetZbuf1, &KtGpuModel::SetZbuf2, &KtGpuModel::SetDrawRegisters};
    for (unsigned i = 0; i < 5; ++i) for (unsigned selector = 0; selector < 2; ++selector) {
        MockMemory mem;
        KtGpuModel model(mem);
        constexpr uint32_t p = 0x2000, data = 0x3000, out = 0x4000;
        for (unsigned j = 0; j < 16; ++j) mem.Store8(0x38c730 + j, j + 1);
        mem.Store32(p, out);
        mem.Store32(data, selector << 13);
        mem.Store32(data + 4, 0xfedcba98);
        mem.Store32(data + 8, 0x76543210);
        mem.Store32(data + 12, 0xfedcba98);
        (model.*methods[i])(p, data);
        Check(mem.Load32(p) == out + 32, "cursor advances by 32 bytes");
        Check(mem.Load32(p + 12) == out, "tag pointer");
        for (unsigned j = 0; j < 16; ++j)
            Check(mem.Load8(out + j) == j + 1, "all 16 tag bytes");
        Check(mem.Load32(out + 16) == (i == 4 ? 0xfedcba98 : 0x76543210), "payload low");
        Check(mem.Load32(out + 20) == (i == 4 ? 0 : 0xfedcba98), "payload high");
        const unsigned bases[]{6, 20, 22, 0x3b, 0x1c};
        Check(mem.Load32(out + 24) == bases[i] + (i < 3 ? selector : 0), "register selector");
        Check(mem.Load32(out + 28) == 0, "register high is zero");
    }
    MockMemory mem;
    KtGpuModel model(mem);
    mem.Store32(0x2000, 0x4000);
    model.ResetTestRegister(0x2000);
    Check(mem.Load32(0x4010) == 0 && mem.Load32(0x4014) == 0 && mem.Load32(0x4018) == 0x3f,
          "reset emits zero payload and register 3f");
}
void test_alias_and_dispatch() {
    MockMemory mem;
    aoe2::gpu::KtGpuModel model(mem);
    mem.Store32(0x2000, 0x4000);
    mem.Store32(0x38c730, 1 << 13);
    mem.Store32(0x38c738, 0xabcdef01);
    mem.Store32(0x38c73c, 0xfedcba98);
    model.SetFrame1(0x2000, 0x4000);
    Check(mem.Load32(0x4018) == 7 && mem.Load32(0x4010) == 0xabcdef01 &&
          mem.Load32(0x4014) == 0xfedcba98, "data reads follow aliased tag writes");
    for (unsigned kind = 0; kind < 29; ++kind) {
        mem.Store8(0x3000, kind);
        Check(model.ValidateCommand(0x3000) == kind, "valid kind");
        mem.Store32(0x38c740 + kind * 4, 0x1000 + kind * 4);
        Check(model.Handler(kind) == 0x1000 + kind * 4, "guest handler table");
    }
    for (unsigned kind : {29, 255}) {
        mem.Store8(0x3000, kind);
        bool rejected = false;
        try { model.ValidateCommand(0x3000); } catch (const std::runtime_error&) { rejected = true; }
        Check(rejected, "invalid kinds fail fast");
    }
    Check(mem.assert_calls == 2, "both invalid kinds report assertions");
    mem.Store32(0x2000, 0x5004);
    model.Begin(0x2000);
    Check(mem.Load32(0x2000) == 0x5020 && mem.Load32(0x2008) == 0x5010,
          "begin aligns and opens DMA packet");
    Check(mem.Load32(0x5010) == 0x10000000, "DMA header");
    model.ResetTestRegister(0x2000);
    model.Finalize(0x2000);
    Check(mem.Load32(0x200c) == 0 && mem.Load32(0x2000) == 0x5040, "finalize cursor and tag clear");
}
void test_command_inspection() {
    MockMemory mem;
    aoe2::gpu::KtGpuModel model(mem);
    mem.Store8(0x3000, 5);
    mem.Store32(0x38c740 + 5 * 4, 0x123456);

    const auto record = model.InspectCommand(0x3000);
    Check(record.kind == 5, "InspectCommand kind matches");
    Check(record.command_addr == 0x3000, "InspectCommand address matches");
    Check(record.handler_addr == 0x123456, "InspectCommand handler matches");
}
}  // namespace
int main() {
    test_packets();
    test_alias_and_dispatch();
    test_command_inspection();
    std::cout << "kt_gpu_model tests passed\n";
}
