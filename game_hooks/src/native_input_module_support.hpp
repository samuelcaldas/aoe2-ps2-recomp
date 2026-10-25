#pragma once

#include <aoe2/game_hooks/kcet_hook_config.hpp>
#include <aoe2/game_hooks/native_module_switches.hpp>
#include "game_overrides.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

namespace aoe2::game_hooks::native_input {

struct FunctionEntry {
    uint32_t address;
    PS2Runtime::RecompiledFunction handler;
    const char* name;
};

struct DeviceRole {
    const char* name;
};

inline NativeModuleSwitches ReadSwitches() {
    const auto path = PS2Runtime::getIoPaths().elfDirectory / "native_modules.ini";
    std::ifstream file(path);
    if (!file.is_open()) return NativeModuleSwitches::AllOff();
    return NativeModuleSwitches::Parse(file);
}

inline KcetHookConfig ReadKcetHooks(const char* module) {
    const auto path = PS2Runtime::getIoPaths().elfDirectory / "kcet_hooks.ini";
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error(std::string("Native kt_") + module +
                                 " requires device hooks; cannot open " + path.string());
    return ParseKcetHookConfig(file);
}

inline bool IsValidPaddingEntry(const char* module, uint32_t address) {
    const std::string_view name(module);
    const bool mouse_padding = address == 0x00134F18 || address == 0x00135D98;
    const bool keyboard_padding = address == 0x00135F90 || address == 0x00137200;
    return (name == "mouse" && mouse_padding) || (name == "keyboard" && keyboard_padding);
}

template <std::size_t EntryCount, std::size_t RoleCount>
void ValidateModule(PS2Runtime& runtime, const char* module,
                    const std::array<FunctionEntry, EntryCount>& entries,
                    const std::array<DeviceRole, RoleCount>& roles) {
    const auto hooks = ReadKcetHooks(module);
    std::set<uint32_t> device_addresses;
    for (const DeviceRole& role : roles) {
        const auto found = hooks.replacements.find(role.name);
        if (found == hooks.replacements.end())
            throw std::runtime_error(std::string("Native kt_") + module + " requires configured device hook " + role.name);
        if (!device_addresses.insert(found->second).second)
            throw std::runtime_error(std::string("Native kt_") + module + " device hook address is duplicated: " + role.name);
        if (!runtime.hasFunction(found->second))
            throw std::runtime_error(std::string("Native kt_") + module + " device hook has no recompiled function: " + role.name);
    }
    for (const FunctionEntry& entry : entries) {
        if (!runtime.hasFunction(entry.address) && !IsValidPaddingEntry(module, entry.address))
            throw std::runtime_error(std::string("Native kt_") + module + " cannot install: missing recompiled function " + entry.name);
    }
}

inline std::array<uint32_t, 32> ReadRegisters(R5900Context* context) {
    std::array<uint32_t, 32> registers{};
    for (uint32_t index = 0; index < 8; ++index) registers[index + 4] = getRegU32(context, index + 4);
    registers[31] = getRegU32(context, 31);
    return registers;
}

}  // namespace aoe2::game_hooks::native_input
