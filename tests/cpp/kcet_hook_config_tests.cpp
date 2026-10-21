#include <aoe2/game_hooks/kcet_hook_config.hpp>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

template <typename Fn>
void Rejects(Fn&& fn) {
    bool rejected = false;
    try { fn(); } catch (const std::exception&) { rejected = true; }
    Check(rejected, "expected invalid hook config to be rejected");
}
}

int main() {
    using aoe2::game_hooks::ParseKcetHookConfig;
    std::istringstream valid("# hooks\n\nkcet.mouse.bind=0x00100000\nkcet.keyboard.read=100004\nalias.scePadRead=0x100008\n");
    const auto config = ParseKcetHookConfig(valid);
    Check(config.replacements.size() == 2, "replacement roles parsed");
    Check(config.aliases.size() == 1, "alias parsed");
    Check(config.replacements.at("kcet.mouse.bind") == 0x00100000u, "hex address parsed");
    Check(config.aliases.at("scePadRead") == 0x00100008u, "alias address parsed");

    for (const std::string text : {
             "bad line\n", "=0x100000\n", "kcet.mouse.bind=\n",
             "kcet.mouse.bind=0x100000\nkcet.mouse.bind=0x100004\n",
             "kcet.mouse.bind=0x100000junk\n", "kcet.mouse.bind=0x100001\n",
             "kcet.mouse.bind=0x100000000\n", "unknown.name=0x100000\n",
             "alias.notAPadCall=0x100000\n", "kcet.keyboard.not_real=0x100000\n",
             "kcet.mouse.bind=0x100000\nextra=\n"}) {
        Rejects([&] { std::istringstream stream(text); ParseKcetHookConfig(stream); });
    }
}
