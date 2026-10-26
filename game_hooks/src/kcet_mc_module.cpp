// Native memory-card boundary: the statically linked Sony libmc (sceMc*).
// The guest library talks to the IOP mcserv over SIF RPC; the emulated IOP has no
// card device (GetInfo returns -11), so the boot stays on the memory card screen.
// These entry points are routed to the runtime's host-backed card (root mc0), which
// follows the SDK calling convention. Other sceMc* entries stay on the guest code.
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "native_input_module_support.hpp"
#include "Stubs/MemoryCard.h"

#include <array>
#include <cstdint>
#include <iostream>

namespace {
struct LibmcEntry {
    uint32_t address;
    PS2Runtime::RecompiledFunction handler;
    const char* name;
};

const std::array<LibmcEntry, 3> kEntries = {{
    {0x0010FF40, &ps2_stubs::sceMcInit, "sceMcInit"},
    {0x00110A78, &ps2_stubs::sceMcGetInfo, "sceMcGetInfo"},
    {0x00110940, &ps2_stubs::sceMcSync, "sceMcSync"},
}};

void InstallNativeMc(PS2Runtime& runtime) {
    const auto switches = aoe2::game_hooks::native_input::ReadSwitches();
    if (!switches.Enabled("memory_card")) {
        std::cout << "[aoe2] native libmc: off\n";
        return;
    }
    std::size_t installed = 0;
    for (const auto& entry : kEntries) {
        if (!runtime.hasFunction(entry.address)) continue;
        if (runtime.replaceFunction(entry.address, entry.handler)) ++installed;
    }
    std::cout << "[aoe2] native libmc: installed " << installed << "/" << kEntries.size() << " functions\n";
}

}  // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native libmc module", "AOK.ELF", 0, 0, InstallNativeMc)
