// Native kt_keybd module adapter; source functions are binary-checked model methods.
#include <aoe2/game_hooks/native_input.hpp>
#include <aoe2/keyboard/kt_keybd_model.hpp>

#include "game_overrides.h"
#include "guest_rdram_access.hpp"
#include "native_input_arguments.hpp"
#include "native_input_module_support.hpp"
#include "state_snapshot_capture.hpp"
#include "ps2_runtime.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using aoe2::game_hooks::native_input::Arguments;
using aoe2::game_hooks::native_input::DeviceRole;
using aoe2::game_hooks::native_input::FunctionEntry;
using aoe2::game_hooks::RdramGuestAccess;
using aoe2::keyboard::KtKeybdModel;

struct NativeKeyboard {
    explicit NativeKeyboard(aoe2::keyboard::KeyboardRpc& rpc) : model(memory, rpc) {}
    RdramGuestAccess memory;
    KtKeybdModel model;
};

std::unique_ptr<NativeKeyboard> g_keyboard;

enum Slot : std::size_t {
#define ENTRY_R(name, address, expression) kSlot_##name,
#define ENTRY_V(name, address, statement) kSlot_##name,
#define DEFERRED(address, reason)
#include "kt_keybd_entries.inc"
#undef ENTRY_R
#undef ENTRY_V
#undef DEFERRED
    kSlotCount
};

std::array<std::atomic<uint64_t>, kSlotCount> g_call_counts{};

bool ShouldLog(uint64_t call_number) {
    return call_number <= 3 || (call_number & (call_number - 1)) == 0;
}

template <typename Body>
void Run(std::size_t slot, const char* name, uint32_t address, const Arguments& arguments,
         uint8_t* rdram, R5900Context* context, bool returns_value, Body body) {
    const uint64_t call_number = ++g_call_counts[slot];
    if (ShouldLog(call_number)) {
        std::cerr << "[native-keyboard] " << name << " n=" << call_number;
        for (std::size_t index = 0; index < 3; ++index)
            std::cerr << " a" << index << "=0x" << std::hex << arguments.U(index);
        std::cerr << std::dec << '\n';
    }
    g_keyboard->memory.Bind(rdram);
    aoe2::game_hooks::BindKeyboardRpcMemory(rdram);
    const uint32_t result = body(g_keyboard->model);
    if (address == 0x00137000)
        aoe2::game_hooks::diagnostics::OnModuleUpdate(rdram, aoe2::game_hooks::synthetic::Module::kKeyboard);
    if (returns_value) setReturnU32(context, result);
    context->pc = getRegU32(context, 31);
}

#define I(index) arguments.I(index)
#define U(index) arguments.U(index)
#define ENTRY_R(name, address, expression) \
    void Handler_##name(uint8_t* rdram, R5900Context* context, PS2Runtime*) { \
        const auto arguments = Arguments::FromRegisters(aoe2::game_hooks::native_input::ReadRegisters(context)); \
        Run(kSlot_##name, #name, address, arguments, rdram, context, true, \
            [&](KtKeybdModel& m) -> uint32_t { return static_cast<uint32_t>(expression); }); \
    }
#define ENTRY_V(name, address, statement) \
    void Handler_##name(uint8_t* rdram, R5900Context* context, PS2Runtime*) { \
        const auto arguments = Arguments::FromRegisters(aoe2::game_hooks::native_input::ReadRegisters(context)); \
        Run(kSlot_##name, #name, address, arguments, rdram, context, false, \
            [&](KtKeybdModel& m) -> uint32_t { statement; return 0; }); \
    }
#define DEFERRED(address, reason)
#include "kt_keybd_entries.inc"
#undef ENTRY_R
#undef ENTRY_V
#undef DEFERRED
#undef I
#undef U

const std::array<FunctionEntry, kSlotCount> kEntries = {{
#define ENTRY_R(name, address, expression) {address, &Handler_##name, #name},
#define ENTRY_V(name, address, statement) {address, &Handler_##name, #name},
#define DEFERRED(address, reason)
#include "kt_keybd_entries.inc"
#undef ENTRY_R
#undef ENTRY_V
#undef DEFERRED
}};

constexpr std::array<DeviceRole, 6> kDeviceRoles{{
    {"kcet.keyboard.bind"}, {"kcet.keyboard.get_info"}, {"kcet.keyboard.read"},
    {"kcet.keyboard.device_info"}, {"kcet.keyboard.set_led"}, {"kcet.keyboard.set_caps_type"},
}};

void InstallNativeKeyboard(PS2Runtime& runtime) {
    const auto switches = aoe2::game_hooks::native_input::ReadSwitches();
    if (!switches.Enabled("keyboard")) {
        std::cout << "[aoe2] native kt_keybd: off\n";
        return;
    }
    aoe2::game_hooks::native_input::ValidateModule(runtime, "keyboard", kEntries, kDeviceRoles);
    g_keyboard = std::make_unique<NativeKeyboard>(aoe2::game_hooks::EnsureKeyboardRpc());
    for (const FunctionEntry& entry : kEntries) {
        if (!runtime.replaceFunction(entry.address, entry.handler))
            throw std::runtime_error(std::string("Native kt_keybd: cannot replace ") + entry.name);
    }
    aoe2::game_hooks::SetNativeKeyboardModuleInstalled(true);
    std::cout << "[aoe2] native kt_keybd: " << kEntries.size() << " functions replaced\n";
}

}  // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native kt_keybd", "AOK.ELF", 0, 0, InstallNativeKeyboard)
