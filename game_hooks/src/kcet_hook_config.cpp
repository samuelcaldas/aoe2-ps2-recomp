#include <aoe2/game_hooks/kcet_hook_config.hpp>

#include <array>
#include <charconv>
#include <stdexcept>
#include <string_view>

namespace aoe2::game_hooks {
namespace {
constexpr std::array<std::string_view, 21> kRoles{
    "kcet.mouse.bind", "kcet.mouse.get_info", "kcet.mouse.read", "kcet.mouse.device_info",
    "kcet.keyboard.bind", "kcet.keyboard.get_info", "kcet.keyboard.read", "kcet.keyboard.device_info",
    "kcet.keyboard.set_led", "kcet.keyboard.set_caps_type",
    "kcet.net.open", "kcet.net.bind", "kcet.net.connect", "kcet.net.send", "kcet.net.recv",
    "kcet.net.recvfrom", "kcet.net.node_id",
    "kcet.sound.init", "kcet.sound.open_stream", "kcet.sound.query_volume", "kcet.sound.query_status"};
constexpr std::array<std::string_view, 29> kPadCalls{
    "scePadEnd", "scePadEnterPressMode", "scePadExitPressMode", "scePadGetButtonMask", "scePadGetDmaStr",
    "scePadGetFrameCount", "scePadGetModVersion", "scePadGetPortMax", "scePadGetReqState", "scePadGetSlotMax",
    "scePadGetState", "scePadInfoAct", "scePadInfoComb", "scePadInfoMode", "scePadInfoPressMode", "scePadInit",
    "scePadInit2", "scePadPortClose", "scePadPortOpen", "scePadRead", "scePadReqIntToStr", "scePadSetActAlign",
    "scePadSetActDirect", "scePadSetButtonInfo", "scePadSetMainMode", "scePadSetReqState", "scePadSetVrefParam",
    "scePadSetWarningLevel", "scePadStateIntToStr"};

std::string_view Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

uint32_t ParseAddress(std::string_view text) {
    if (text.starts_with("0x") || text.starts_with("0X")) text.remove_prefix(2);
    if (text.empty()) throw std::invalid_argument("empty address");
    uint32_t address = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), address, 16);
    if (error != std::errc{} || end != text.data() + text.size() || address % 4 != 0)
        throw std::invalid_argument("invalid KCET hook address");
    return address;
}

void ValidateName(std::string_view name, bool& alias, std::string& normalized) {
    for (const auto role : kRoles) if (name == role) { normalized = role; return; }
    constexpr std::string_view prefix = "alias.";
    if (name.starts_with(prefix)) {
        const auto candidate = name.substr(prefix.size());
        for (const auto call : kPadCalls) if (candidate == call) { alias = true; normalized = candidate; return; }
    }
    throw std::invalid_argument("unknown KCET hook name");
}
}

KcetHookConfig ParseKcetHookConfig(std::istream& input) {
    KcetHookConfig config;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        const auto text = Trim(line);
        if (text.empty() || text.front() == '#') continue;
        const auto separator = text.find('=');
        if (separator == std::string_view::npos || text.find('=', separator + 1) != std::string_view::npos)
            throw std::invalid_argument("malformed KCET hook config line " + std::to_string(line_number));
        const auto name = Trim(text.substr(0, separator));
        const auto value = Trim(text.substr(separator + 1));
        if (name.empty() || value.empty()) throw std::invalid_argument("empty KCET hook key/value");
        bool alias = false;
        std::string normalized;
        ValidateName(name, alias, normalized);
        const uint32_t address = ParseAddress(value);
        auto& entries = alias ? config.aliases : config.replacements;
        if (!entries.emplace(normalized, address).second) throw std::invalid_argument("duplicate KCET hook key");
    }
    if (input.bad()) throw std::runtime_error("failed reading KCET hook config");
    return config;
}
}
