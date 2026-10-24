#include <aoe2/game_hooks/native_module_switches.hpp>

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

using aoe2::game_hooks::NativeModuleSwitches;

namespace {
int failures = 0;

void Check(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << message << '\n';
    ++failures;
}

void Rejects(std::string_view text) {
    std::istringstream input{std::string(text)};
    try {
        NativeModuleSwitches::Parse(input);
        Check(false, "invalid configuration was accepted");
    } catch (const std::invalid_argument&) {
    }
}
}

int main() {
    std::istringstream empty;
    auto switches = NativeModuleSwitches::Parse(empty);
    for (auto module : aoe2::game_hooks::kNativeModuleNames) Check(!switches.Enabled(module), "empty config enables a module");
    std::istringstream configured{"# c\n\nkernel=on\nnet=off\n"};
    switches = NativeModuleSwitches::Parse(configured);
    Check(switches.Enabled("kernel"), "kernel should be enabled");
    Check(!switches.Enabled("net") && !switches.Enabled("pad"), "off or unspecified module enabled");
    std::istringstream pad_device_config{"pad_device=on\n"};
    switches = NativeModuleSwitches::Parse(pad_device_config);
    Check(switches.Enabled("pad_device"), "pad_device should be enabled");
    Check(!switches.Enabled("pad"), "pad_device must not enable pad");
    switches = NativeModuleSwitches::AllOff();
    Check(!switches.Enabled("kernel"), "AllOff enabled kernel");
    for (auto invalid : {"kernel=maybe\n", "unknown=on\n", "kernel=on\nkernel=off\n", "kernel\n", "=on\n", "kernel=\n"}) Rejects(invalid);
    try {
        switches.Enabled("unknown");
        Check(false, "unknown module was accepted");
    } catch (const std::invalid_argument&) {
    }
    return failures;
}
