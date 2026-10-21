#pragma once

#include <cstdint>
#include <istream>
#include <map>
#include <string>

namespace aoe2::game_hooks {
struct KcetHookConfig {
    std::map<std::string, uint32_t> replacements;
    std::map<std::string, uint32_t> aliases;
};

KcetHookConfig ParseKcetHookConfig(std::istream& input);
}
