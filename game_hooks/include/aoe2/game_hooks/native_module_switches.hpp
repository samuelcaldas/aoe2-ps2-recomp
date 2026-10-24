#pragma once

#include <array>
#include <istream>
#include <string_view>

namespace aoe2::game_hooks {

inline constexpr std::array<std::string_view, 9> kNativeModuleNames{
    "kernel", "pad", "mouse", "keyboard", "file_cd", "memory_card", "hd", "gpu", "net"};

class NativeModuleSwitches {
public:
    static NativeModuleSwitches Parse(std::istream& input);
    static NativeModuleSwitches AllOff();
    bool Enabled(std::string_view module) const;

private:
    explicit NativeModuleSwitches(std::array<bool, kNativeModuleNames.size()> enabled);
    std::array<bool, kNativeModuleNames.size()> enabled_{};
};

}
