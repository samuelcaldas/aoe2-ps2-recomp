#include <aoe2/cdvd/cdvd_model.hpp>

#include "game_overrides.h"
#include "guest_rdram_access.hpp"
#include "native_input_arguments.hpp"
#include "native_input_module_support.hpp"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"

// Access the shared pending-NCmd-signal counter (filled by SIF HLE when GetToc fires before
// the NCmd completion sema at D_4ED348 is created). We drain it in the hook for func_0012BC10,
// which thread 7 calls immediately before its first WaitSema(74).
#include "Stubs/SifCdvdServer.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

namespace {
using aoe2::cdvd::CdvdModel;
using aoe2::game_hooks::native_input::Arguments;
using aoe2::game_hooks::native_input::FunctionEntry;
using aoe2::game_hooks::RdramGuestAccess;

struct NativeCdvd {
    explicit NativeCdvd() : model(memory) {}
    RdramGuestAccess memory;
    CdvdModel model;
};

std::unique_ptr<NativeCdvd> g_cdvd;

enum Slot : std::size_t {
#define ENTRY_R(name, address, expression) kSlot_##name,
#define ENTRY_V(name, address, statement) kSlot_##name,
#define DEFERRED(address, reason)
#include "kt_cdvd_entries.inc"
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
        std::cerr << "[native-cdvd] " << name << " n=" << call_number;
        for (std::size_t index = 0; index < 3; ++index)
            std::cerr << " a" << index << "=0x" << std::hex << arguments.U(index);
        std::cerr << std::dec << '\n';
    }
    g_cdvd->memory.Bind(rdram);
    const uint32_t result = body(g_cdvd->model);
    if (returns_value) setReturnU32(context, result);
    context->pc = getRegU32(context, 31);
}

#define I(index) arguments.I(index)
#define U(index) arguments.U(index)
#define ENTRY_R(name, address, expression) \
    void Handler_##name(uint8_t* rdram, R5900Context* context, PS2Runtime*) { \
        const auto arguments = Arguments::FromRegisters(aoe2::game_hooks::native_input::ReadRegisters(context)); \
        Run(kSlot_##name, #name, address, arguments, rdram, context, true, \
            [&](CdvdModel& m) -> uint32_t { return static_cast<uint32_t>(expression); }); \
    }
#define ENTRY_V(name, address, statement) \
    void Handler_##name(uint8_t* rdram, R5900Context* context, PS2Runtime*) { \
        const auto arguments = Arguments::FromRegisters(aoe2::game_hooks::native_input::ReadRegisters(context)); \
        Run(kSlot_##name, #name, address, arguments, rdram, context, false, \
            [&](CdvdModel& m) -> uint32_t { statement; return 0; }); \
    }
#define DEFERRED(address, reason)
#include "kt_cdvd_entries.inc"
#undef ENTRY_R
#undef ENTRY_V
#undef DEFERRED
#undef I
#undef U

const std::array<FunctionEntry, kSlotCount> kEntries = {{
#define ENTRY_R(name, address, expression) {address, &Handler_##name, #name},
#define ENTRY_V(name, address, statement) {address, &Handler_##name, #name},
#define DEFERRED(address, reason)
#include "kt_cdvd_entries.inc"
#undef ENTRY_R
#undef ENTRY_V
#undef DEFERRED
}};

// func_0012BC10: reads D_4ED348 (NCmd completion sema ID) and returns it.
// Thread 7 (func_0012B648) calls this immediately before its first WaitSema. We hook it to
// flush any NCmd completions that were deferred in the SIF HLE while the sema didn't exist yet
// (race: GetToc NCmd fires before func_0012B8A8 writes the sema ID into D_4ED348).
constexpr uint32_t kFunc0012BC10 = 0x0012BC10u;

void Handler_FlushPendingNcmdSignals(uint8_t* rdram, R5900Context* context, PS2Runtime* runtime) {
    // Replicate func_0012BC10: return *(int32_t*)(0x4F0000 - 0x2CB8) = *(int32_t*)(0x4ED348).
    uint32_t ncmd_sema = 0u;
    std::memcpy(&ncmd_sema, rdram + (0x4ED348u & 0x01FFFFFFu), sizeof(ncmd_sema));
    const int32_t sema_id = static_cast<int32_t>(ncmd_sema);

    if (sema_id > 0 && runtime) {
        const uint32_t pending =
            ps2_sif::SifCdvdServer::s_pendingNcmdSignals.exchange(0u, std::memory_order_relaxed);
        if (pending > 0u) {
            std::cerr << "[cdvd:bc10] flushing " << pending
                      << " deferred NCmd signal(s) into sema " << sema_id << '\n';
            for (uint32_t i = 0u; i < pending; ++i)
                (void)runtime->eeScheduler().signalSemaphore(sema_id, true);
        }
    }

    setReturnU32(context, static_cast<uint32_t>(sema_id));
    context->pc = getRegU32(context, 31);
}

void InstallNativeCdvd(PS2Runtime& runtime) {
    const auto switches = aoe2::game_hooks::native_input::ReadSwitches();
    const bool enabled = switches.Enabled("cdvd") || switches.Enabled("file_cd");
    if (!enabled) {
        std::cout << "[aoe2] native kt_libcdvd_server: off\n";
        return;
    }
    g_cdvd = std::make_unique<NativeCdvd>();
    const auto disc_root = PS2Runtime::getIoPaths().elfDirectory;
    g_cdvd->model.Initialize(disc_root.string());

    std::size_t installed = 0;
    for (const auto& entry : kEntries) {
        if (!runtime.hasFunction(entry.address)) continue;
        if (runtime.replaceFunction(entry.address, entry.handler)) ++installed;
    }
    // Hook func_0012BC10 (returns the NCmd completion sema ID). Thread 7 calls this
    // immediately before WaitSema(74). We flush any deferred NCmd signals here so that
    // completions that fired before the sema was created are not lost.
    if (runtime.hasFunction(kFunc0012BC10) &&
        runtime.replaceFunction(kFunc0012BC10, Handler_FlushPendingNcmdSignals)) {
        std::cout << "[aoe2] native kt_libcdvd_server: hooked func_0012BC10 (NCmd sema flush)\n";
    }
    std::cout << "[aoe2] native kt_libcdvd_server: installed " << installed << "/" << kEntries.size() << " functions\n";
}

}  // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native cdvd module", "AOK.ELF", 0, 0, InstallNativeCdvd)
