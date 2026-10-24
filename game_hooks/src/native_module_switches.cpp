#include <aoe2/game_hooks/native_module_switches.hpp>

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

namespace aoe2::game_hooks {
namespace {
std::string_view Trim(std::string_view value) {
    const auto non_space = [](unsigned char character) { return !std::isspace(character); };
    const auto first = std::find_if(value.begin(), value.end(), non_space);
    const auto last = std::find_if(value.rbegin(), value.rend(), non_space).base();
    if (first >= last) return {};
    return {&*first, static_cast<std::size_t>(last - first)};
}

std::size_t ModuleIndex(std::string_view name) {
    const auto found = std::find(kNativeModuleNames.begin(), kNativeModuleNames.end(), name);
    if (found == kNativeModuleNames.end()) throw std::invalid_argument("unknown native module: " + std::string(name));
    return static_cast<std::size_t>(found - kNativeModuleNames.begin());
}
}

NativeModuleSwitches::NativeModuleSwitches(std::array<bool, kNativeModuleNames.size()> enabled)
    : enabled_(enabled) {}

NativeModuleSwitches NativeModuleSwitches::AllOff() {
    return NativeModuleSwitches({});
}

NativeModuleSwitches NativeModuleSwitches::Parse(std::istream& input) {
    auto result = AllOff();
    std::array<bool, kNativeModuleNames.size()> seen{};
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto content = Trim(line);
        if (content.empty() || content.front() == '#') continue;
        const auto separator = content.find('=');
        if (separator == std::string_view::npos) throw std::invalid_argument("invalid native module switch line " + std::to_string(line_number));
        const auto name = Trim(content.substr(0, separator));
        const auto value = Trim(content.substr(separator + 1));
        const auto index = ModuleIndex(name);
        if (seen[index]) throw std::invalid_argument("duplicate native module switch: " + std::string(name));
        if (value != "on" && value != "off") throw std::invalid_argument("invalid value for native module switch: " + std::string(name));
        seen[index] = true;
        result.enabled_[index] = value == "on";
    }
    if (input.bad()) throw std::invalid_argument("failed reading native module switches");
    return result;
}

bool NativeModuleSwitches::Enabled(std::string_view module) const {
    return enabled_[ModuleIndex(module)];
}

}
