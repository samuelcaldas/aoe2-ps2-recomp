#include <aoe2/game_hooks/kcet_handlers.hpp>
#include <aoe2/game_hooks/kcet_hook_config.hpp>
#include "game_overrides.h"
#include "ps2_runtime_macros.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#if defined(__SWITCH__)
#include <switch.h>
#endif

// Defined by the generated overlay registration, or by overlay_stub.cpp when none exist.
int RegisterGeneratedOverlays(PS2Runtime& runtime);

namespace {
using aoe2::game_hooks::KcetHandlers;
using aoe2::game_hooks::KcetHookConfig;

// Fast-path native hook for the 5577-slot linear object allocator at 0x263650.
// In the unoptimized recompiled binary, this 5577-iteration linear search was split across
// 4 functions, incurring >22 million function dispatches through PS2Runtime during
// FUN_0032f6b0 level data loading. This native implementation completes the search in microseconds.
void FastFunc_0x263650(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {
    static bool s_announced = false;
    if (!s_announced) {
        std::cout << "[aoe2] fast-path entity allocator (0x263650) active" << std::endl;
        s_announced = true;
    }
    const uint32_t base = GPR_U32(ctx, 4);
    for (uint32_t i = 0; i < 5577; ++i) {
        const uint32_t slot = base + i * 64;
        if (slot + 0x3d < 0x04000000) {
            const uint8_t flags = rdram[slot + 0x3d];
            if ((flags & 0x08) == 0) {
                rdram[slot + 0x3d] = flags | 0x08;
                SET_GPR_U32(ctx, 2, slot); // $v0: allocated slot pointer
                SET_GPR_U32(ctx, 4, slot); // $a0: slot pointer (delay slot behavior)
                ctx->pc = GPR_U32(ctx, 31); // jr $ra
                return;
            }
        }
    }
    SET_GPR_U32(ctx, 2, 0); // $v0 = NULL
    ctx->pc = GPR_U32(ctx, 31); // jr $ra
}

KcetHandlers host_handlers() {
    KcetHandlers handlers;
    aoe2::game_hooks::RegisterMouseHandlers(handlers);
    aoe2::game_hooks::RegisterKeyboardHandlers(handlers);
    aoe2::game_hooks::RegisterNetworkHandlers(handlers);
    aoe2::game_hooks::RegisterSoundHandlers(handlers);
    return handlers;
}

KcetHookConfig load_config() {
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] load_config ENTER\n", 33);
#endif
    const auto elfDir = PS2Runtime::getIoPaths().elfDirectory;
#if defined(__SWITCH__)
    {
        std::string p_str = "[aoe2-switch] elfDirectory: " + elfDir.string() + "\n";
        svcOutputDebugString(p_str.c_str(), p_str.length());
    }
#endif
    const auto path = elfDir / "kcet_hooks.ini";
#if defined(__SWITCH__)
    {
        std::string p_str = "[aoe2-switch] opening: " + path.string() + "\n";
        svcOutputDebugString(p_str.c_str(), p_str.length());
    }
#endif
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open " + path.string() + "; run tools/resolve_kcet_hooks.py");
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] file is_open SUCCESS, parsing config\n", 51);
#endif
    return aoe2::game_hooks::ParseKcetHookConfig(file);
}

void apply_replacements(PS2Runtime& runtime, const KcetHookConfig& config, const KcetHandlers& handlers) {
    for (const auto& [name, address] : config.replacements) {
        const auto handler = handlers.find(name);
        if (handler == handlers.end()) throw std::invalid_argument("Unregistered KCET hook: " + name);
        if (!runtime.replaceFunction(address, handler->second))
            throw std::runtime_error("Cannot apply KCET hook: " + name);
    }
}

void apply_aliases(PS2Runtime& runtime, const KcetHookConfig& config) {
    for (const auto& [name, address] : config.aliases) {
        if (!ps2_game_overrides::bindAddressHandler(runtime, address, name))
            throw std::runtime_error("Cannot apply KCET alias: " + name);
    }
}

void apply_kcet_hooks(PS2Runtime& runtime) {
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] apply_kcet_hooks START\n", 37);
#endif
    try {
        const auto config = load_config();
#if defined(__SWITCH__)
        svcOutputDebugString("[aoe2-switch] load_config() SUCCESS\n", 36);
#endif
        const auto handlers = host_handlers();
        aoe2::game_hooks::SetMousePresentationSource(runtime);
        apply_replacements(runtime, config, handlers);
#if defined(__SWITCH__)
        svcOutputDebugString("[aoe2-switch] apply_replacements() SUCCESS\n", 43);
#endif
        apply_aliases(runtime, config);
#if defined(__SWITCH__)
        svcOutputDebugString("[aoe2-switch] apply_aliases() SUCCESS\n", 38);
#endif
        // Fast-path allocator hook for mission entity loading
        if (!runtime.replaceFunction(0x263650u, FastFunc_0x263650)) {
            std::cerr << "[aoe2] Warning: failed to replace 0x263650 fast allocator\n";
        }
        const int overlays = RegisterGeneratedOverlays(runtime);
#if defined(__SWITCH__)
        svcOutputDebugString("[aoe2-switch] RegisterGeneratedOverlays() SUCCESS\n", 50);
#endif
        std::cout << "[aoe2] KCET hooks applied: " << config.replacements.size()
                  << "/" << handlers.size() << "; aliases: " << config.aliases.size() << "; overlays: " << overlays << '\n';
    } catch (const std::exception& e) {
#if defined(__SWITCH__)
        std::string err = std::string("[FATAL-KCET-HOOKS] Exception: ") + e.what() + "\n";
        svcOutputDebugString(err.c_str(), err.length());
#endif
        std::cerr << "[aoe2] apply_kcet_hooks exception: " << e.what() << '\n';
        throw;
    }
}
}

PS2_REGISTER_GAME_OVERRIDE("aoe2 KCET hooks", "AOK.ELF", 0, 0, apply_kcet_hooks)
