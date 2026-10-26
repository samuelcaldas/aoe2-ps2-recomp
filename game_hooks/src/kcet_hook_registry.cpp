#include <aoe2/game_hooks/kcet_handlers.hpp>
#include <aoe2/game_hooks/kcet_hook_config.hpp>
#include "game_overrides.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Defined by the generated overlay registration, or by overlay_stub.cpp when none exist.
int RegisterGeneratedOverlays(PS2Runtime& runtime);

namespace {
using aoe2::game_hooks::KcetHandlers;
using aoe2::game_hooks::KcetHookConfig;

KcetHandlers host_handlers() {
    KcetHandlers handlers;
    aoe2::game_hooks::RegisterMouseHandlers(handlers);
    aoe2::game_hooks::RegisterKeyboardHandlers(handlers);
    aoe2::game_hooks::RegisterNetworkHandlers(handlers);
    aoe2::game_hooks::RegisterSoundHandlers(handlers);
    return handlers;
}

KcetHookConfig load_config() {
    const auto path = PS2Runtime::getIoPaths().elfDirectory / "kcet_hooks.ini";
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open " + path.string() + "; run tools/resolve_kcet_hooks.py");
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
    const auto config = load_config();
    const auto handlers = host_handlers();
    apply_replacements(runtime, config, handlers);
    apply_aliases(runtime, config);
    const int overlays = RegisterGeneratedOverlays(runtime);
    std::cout << "[aoe2] KCET hooks applied: " << config.replacements.size()
              << "/" << handlers.size() << "; aliases: " << config.aliases.size() << "; overlays: " << overlays << '\n';
}
}

PS2_REGISTER_GAME_OVERRIDE("aoe2 KCET hooks", "AOK.ELF", 0, 0, apply_kcet_hooks)
