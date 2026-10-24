// Native Sony libpad handlers. The guest kt_libpad is generated and remains
// responsible for all PadSlot state; this adapter replaces only Sony services.
// Origin of the consumed call contract: samuelcaldas/age-of-empires-2-decomp,
// kt_libpad.c at 5d857d49e8c2d1633c0a3e0982ddb50b40d8cbd0 (clean source tree).
//
// Scheduling: these device calls are synchronous and never block. Set pc to ra
// before dispatch, matching the kernel adapter contract. kt_libpad's guest-side
// mutex is outside this boundary and is not acquired by these handlers.
#include <aoe2/game_hooks/kcet_pad_backend.hpp>
#include <aoe2/game_hooks/native_module_switches.hpp>
#include <aoe2/pad/pad_device.hpp>

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/ps2_memory.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <unordered_map>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using aoe2::pad::PadDevice;
constexpr int kRegA0 = 4;
constexpr int kRegRa = 31;

#define PAD_DEVICE_ENTRY(name, address) constexpr uint32_t kAddress_##name = address;
#include "pad_device_entries.def"
#undef PAD_DEVICE_ENTRY

struct NativePad {
    explicit NativePad(std::unique_ptr<aoe2::pad::PadBackend> host_backend)
        : backend(std::move(host_backend)), device(*backend) {}
    std::unique_ptr<aoe2::pad::PadBackend> backend;
    PadDevice device;
};

std::unique_ptr<NativePad> g_pad;

aoe2::game_hooks::NativeModuleSwitches LoadSwitches() {
    const auto path = PS2Runtime::getIoPaths().elfDirectory / "native_modules.ini";
    std::ifstream file(path);
    if (!file.is_open()) return aoe2::game_hooks::NativeModuleSwitches::AllOff();
    return aoe2::game_hooks::NativeModuleSwitches::Parse(file);
}

int Arg(R5900Context* ctx, int index) {
    return static_cast<int>(getRegU32(ctx, kRegA0 + index));
}
uint32_t UArg(R5900Context* ctx, int index) {
    return getRegU32(ctx, kRegA0 + index);
}

template <typename Body>
void Run(const char* name, uint8_t* rdram, R5900Context* ctx, Body body) {
    const uint32_t return_address = getRegU32(ctx, kRegRa);
    ctx->pc = return_address;
    const int result = body(*g_pad, rdram, ctx);
    setReturnU32(ctx, static_cast<uint32_t>(result));
    ctx->pc = return_address;
    static std::unordered_map<const char*, uint64_t> counts;
    const uint64_t n = ++counts[name];
    if (n <= 3 || (n & (n - 1)) == 0)
        std::cerr << "[native-pad-device] " << name << " n=" << n << " result=" << result << '\n';
}

#define PAD_HANDLER(name, ...) \
    void Handler_##name(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) { \
        Run(#name, rdram, ctx, [](NativePad& pad, uint8_t* memory, R5900Context* c) -> int { \
            (void)memory; (void)c; __VA_ARGS__ \
        }); \
    }

PAD_HANDLER(init, return pad.device.Init(Arg(c, 0));)
PAD_HANDLER(start, return pad.device.Start(Arg(c, 0));)
PAD_HANDLER(slot_max, return pad.device.SlotMax(Arg(c, 0));)
PAD_HANDLER(port_open, return pad.device.PortOpen(Arg(c, 0), Arg(c, 1), UArg(c, 2));)
PAD_HANDLER(port_close, return pad.device.PortClose(Arg(c, 0), Arg(c, 1));)
PAD_HANDLER(state, return pad.device.State(Arg(c, 0), Arg(c, 1));)
PAD_HANDLER(read,
    std::array<uint8_t, 32> report{};
    const int result = pad.device.Read(Arg(c, 0), Arg(c, 1), report);
    if (result > 0) {
        uint8_t* destination = getMemPtr(memory, UArg(c, 2));
        if (destination == nullptr) throw std::runtime_error("Sony pad read: invalid guest report pointer");
        std::copy(report.begin(), report.end(), destination);
    }
    return result;)
PAD_HANDLER(req_state, return pad.device.ReqState(Arg(c, 0), Arg(c, 1));)
PAD_HANDLER(info_act, return pad.device.InfoAct(Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3));)
PAD_HANDLER(info_mode, return pad.device.InfoMode(Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3));)
PAD_HANDLER(set_main_mode, return pad.device.SetMainMode(Arg(c, 0), Arg(c, 1), Arg(c, 2), Arg(c, 3));)
PAD_HANDLER(set_act_direct,
    const uint8_t* data = getMemPtr(memory, UArg(c, 2));
    if (data == nullptr) throw std::runtime_error("Sony pad actuator: invalid guest data pointer");
    return pad.device.SetActDirect(Arg(c, 0), Arg(c, 1), data);)
PAD_HANDLER(set_act_align,
    const uint8_t* data = getMemPtr(memory, UArg(c, 2));
    if (data == nullptr) throw std::runtime_error("Sony pad alignment: invalid guest data pointer");
    return pad.device.SetActAlign(Arg(c, 0), Arg(c, 1), data);)
PAD_HANDLER(info_press_mode, return pad.device.InfoPressMode(Arg(c, 0), Arg(c, 1));)
#undef PAD_HANDLER

struct Entry { uint32_t address; PS2Runtime::RecompiledFunction handler; const char* name; };
const std::array<Entry, 14> kEntries = {{
#define PAD_DEVICE_ENTRY(name, address) {address, &Handler_##name, #name},
#include "pad_device_entries.def"
#undef PAD_DEVICE_ENTRY
}};

void install_native_pad_device(PS2Runtime& runtime) {
    if (!LoadSwitches().Enabled("pad_device")) {
        std::cout << "[aoe2] native pad device: off\n";
        return;
    }
    for (const Entry& entry : kEntries) {
        if (!runtime.hasFunction(entry.address))
            std::cout << "[aoe2] native pad device: " << entry.name
                      << " has no recompiled original (replacement only)\n";
    }
    const char* synthetic = std::getenv("AOE2_PAD_SYNTHETIC");
    const bool use_synthetic = synthetic != nullptr && synthetic[0] == '1';
    std::cout << "[aoe2] native pad device backend: " << (use_synthetic ? "synthetic" : "raylib") << '\n';
    g_pad = std::make_unique<NativePad>(use_synthetic ? aoe2::game_hooks::MakeSyntheticPadBackend()
                                                      : aoe2::game_hooks::MakeRaylibPadBackend());
    for (const Entry& entry : kEntries) {
        if (!runtime.replaceFunction(entry.address, entry.handler))
            throw std::runtime_error(std::string("Native pad device: cannot replace ") + entry.name);
    }
    std::cout << "[aoe2] native pad device: " << kEntries.size() << " functions replaced\n";
}
} // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native pad device", "AOK.ELF", 0, 0,
                           install_native_pad_device)
