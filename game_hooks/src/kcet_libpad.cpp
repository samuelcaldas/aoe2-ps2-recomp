// Native replacement of the KCET gamepad module (kt_libpad): handlers over guest memory.
//
// The logic lives in pad/ (KtLibpadModel); this file only reads the arguments from the context, calls the model
// and returns. Switches (native_modules.ini): "pad=on" together with "pad_device=on". Nothing here blocks, so
// each handler sets pc to the return address and returns in v0; the model is atomic inside a handler, which
// is why the module's semaphore lock is not taken.
#include <aoe2/game_hooks/native_module_switches.hpp>
#include <aoe2/game_hooks/native_pad.hpp>
#include <aoe2/pad/kt_libpad_model.hpp>

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"

#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

using aoe2::pad::KtLibpadModel;
using aoe2::pad::PadGuestMemory;

constexpr int kRegA0 = 4;
constexpr int kRegRa = 31;

// Guest RAM seen through the runtime's address translation; a bad address is reported once and reads as zero.
class RdramPadMemory final : public PadGuestMemory {
public:
    void Bind(uint8_t* rdram) { rdram_ = rdram; }
    uint8_t Load8(uint32_t address) override { return Load<uint8_t>(address); }
    uint16_t Load16(uint32_t address) override { return Load<uint16_t>(address); }
    uint32_t Load32(uint32_t address) override { return Load<uint32_t>(address); }
    void Store8(uint32_t address, uint8_t value) override { Store(address, value); }
    void Store16(uint32_t address, uint16_t value) override { Store(address, value); }
    void Store32(uint32_t address, uint32_t value) override { Store(address, value); }
    void ReadBytes(uint32_t address, uint8_t* out, std::size_t count) override {
        for (std::size_t i = 0; i < count; ++i) out[i] = Load8(address + static_cast<uint32_t>(i));
    }
    void WriteBytes(uint32_t address, const uint8_t* in, std::size_t count) override {
        for (std::size_t i = 0; i < count; ++i) Store8(address + static_cast<uint32_t>(i), in[i]);
    }
    void ReportAssertion(std::string_view file, int line, std::string_view expression) override {
        std::cerr << "[native-libpad] ASSERT " << file << ':' << line << " (0x" << std::hex << line << std::dec << ") "
                  << expression << std::endl;
    }

private:
    template <typename T>
    T Load(uint32_t address) {
        T value{};
        const uint8_t* source = getMemPtr(rdram_, address);
        if (source == nullptr) return Bad(address), value;
        std::memcpy(&value, source, sizeof(T));
        return value;
    }
    template <typename T>
    void Store(uint32_t address, T value) {
        uint8_t* destination = getMemPtr(rdram_, address);
        if (destination == nullptr) return Bad(address);
        std::memcpy(destination, &value, sizeof(T));
    }
    void Bad(uint32_t address) {
        if (reported_++ < 8) std::cerr << "[native-libpad] bad guest address 0x" << std::hex << address << std::dec << std::endl;
    }
    uint8_t* rdram_ = nullptr;
    int reported_ = 0;
};

struct NativeLibpad {
    explicit NativeLibpad(aoe2::pad::PadDevice& device) : model(memory, device) {}
    RdramPadMemory memory;
    KtLibpadModel model;
};

std::unique_ptr<NativeLibpad> g_libpad;

enum Slot : std::size_t {
#define LIBPAD_R(name, address, expression) kSlot_##name,
#define LIBPAD_V(name, address, statement) kSlot_##name,
#define LIBPAD_DEFERRED(address, reason)
#include "kt_libpad_entries.inc"
#undef LIBPAD_R
#undef LIBPAD_V
#undef LIBPAD_DEFERRED
    kSlotCount
};

std::array<std::atomic<uint64_t>, kSlotCount> g_call_counts{};

bool ShouldLog(uint64_t call_number) { return call_number <= 3 || (call_number & (call_number - 1)) == 0; }

template <typename Body>
void Run(std::size_t slot, const char* name, uint8_t* rdram, R5900Context* ctx, bool returns_value, Body body) {
    const uint64_t call_number = ++g_call_counts[slot];
    if (ShouldLog(call_number))
        std::cerr << "[native-libpad] " << name << " n=" << call_number << " a0=0x" << std::hex << getRegU32(ctx, kRegA0)
                  << std::dec << std::endl;
    g_libpad->memory.Bind(rdram);
    const uint32_t return_address = getRegU32(ctx, kRegRa);
    const uint32_t result = body(g_libpad->model, ctx);
    if (returns_value) setReturnU32(ctx, result);
    ctx->pc = return_address;
}

#define I(n) static_cast<int>(getRegU32(c, kRegA0 + (n)))
#define U(n) getRegU32(c, kRegA0 + (n))

#define LIBPAD_R(name, address, expression)                                                                           \
    void Handler_##name(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {                                            \
        Run(kSlot_##name, #name, rdram, ctx, true,                                                                   \
            [](KtLibpadModel& m, R5900Context* c) -> uint32_t { (void)m; (void)c; return static_cast<uint32_t>(expression); }); \
    }
#define LIBPAD_V(name, address, statement)                                                                            \
    void Handler_##name(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {                                            \
        Run(kSlot_##name, #name, rdram, ctx, false,                                                                  \
            [](KtLibpadModel& m, R5900Context* c) -> uint32_t { (void)m; (void)c; statement; return 0; });          \
    }
#define LIBPAD_DEFERRED(address, reason)
#include "kt_libpad_entries.inc"
#undef LIBPAD_R
#undef LIBPAD_V
#undef LIBPAD_DEFERRED
#undef I
#undef U

struct Entry {
    uint32_t address;
    PS2Runtime::RecompiledFunction handler;
    const char* name;
};

const std::array<Entry, kSlotCount> kEntries = {{
#define LIBPAD_R(name, address, expression) {address, &Handler_##name, #name},
#define LIBPAD_V(name, address, statement) {address, &Handler_##name, #name},
#define LIBPAD_DEFERRED(address, reason)
#include "kt_libpad_entries.inc"
#undef LIBPAD_R
#undef LIBPAD_V
#undef LIBPAD_DEFERRED
}};

aoe2::game_hooks::NativeModuleSwitches LoadSwitches() {
    const auto path = PS2Runtime::getIoPaths().elfDirectory / "native_modules.ini";
    std::ifstream file(path);
    if (!file.is_open()) return aoe2::game_hooks::NativeModuleSwitches::AllOff();
    return aoe2::game_hooks::NativeModuleSwitches::Parse(file);
}

void install_native_libpad(PS2Runtime& runtime) {
    const auto switches = LoadSwitches();
    if (!switches.Enabled("pad")) {
        std::cout << "[aoe2] native kt_libpad: off\n";
        return;
    }
    if (!switches.Enabled("pad_device")) {
        std::cout << "[aoe2] native kt_libpad: needs pad_device=on as well; nothing installed\n";
        return;
    }
    aoe2::pad::PadDevice& device = aoe2::game_hooks::EnsureNativePadDevice(runtime);
    for (const Entry& entry : kEntries) {
        if (!runtime.hasFunction(entry.address))
            std::cout << "[aoe2] native kt_libpad: " << entry.name << " has no recompiled original (replacement only)\n";
    }
    g_libpad = std::make_unique<NativeLibpad>(device);
    for (const Entry& entry : kEntries) {
        if (!runtime.replaceFunction(entry.address, entry.handler))
            throw std::runtime_error(std::string("Native kt_libpad: cannot replace ") + entry.name);
    }
    std::cout << "[aoe2] native kt_libpad: " << kEntries.size() << " functions replaced\n";
}

}  // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native kt_libpad", "AOK.ELF", 0, 0, install_native_libpad)
