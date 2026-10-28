// Runner behavior adapted from the pinned PS2Recomp runner (GPL-3.0).
#include "game_entry.hpp"
#include "games_database.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace aoe2::platform {
std::filesystem::path game_path(int count, char* arguments[]) {
    if (count >= 2 && arguments[1] && arguments[1][0] != '\0') return arguments[1];
#if defined(PS2X_DEFAULT_BOOT_ELF)
    const std::filesystem::path configured = PS2X_DEFAULT_BOOT_ELF;
    if (configured.is_absolute()) return configured;
#if defined(__SWITCH__)
    // On Nintendo Switch, check sdmc:/switch/aoe2/<configured> first
    const auto sdmc_candidate = std::filesystem::path("sdmc:/switch/aoe2") / configured;
    if (std::filesystem::exists(sdmc_candidate)) return sdmc_candidate;
#endif
    return (std::filesystem::current_path() / configured).lexically_normal();
#else
    throw std::runtime_error("Pass the guest ELF as argv[1] or define PS2X_DEFAULT_BOOT_ELF");
#endif
}

std::filesystem::path disc_image_path(int count, char* arguments[]) {
    if (count >= 3 && arguments[2] && arguments[2][0] != '\0') {
        const std::filesystem::path image = arguments[2];
        if (!std::filesystem::is_regular_file(image))
            throw std::runtime_error("Disc image not found: " + image.string());
        return image;
    }
#if defined(__SWITCH__)
    // Check default disc image location on Switch SDMC
    const auto default_iso = std::filesystem::path("sdmc:/switch/aoe2/aoe2.iso");
    if (std::filesystem::is_regular_file(default_iso)) return default_iso;
#endif
    return {};
}

std::string game_window_title(const std::filesystem::path& path) {
    const auto filename = path.filename().string();
    auto identifier = filename;
    const auto underscore = identifier.find('_');
    if (underscore != std::string::npos) identifier[underscore] = '-';
    const auto dot = identifier.find('.');
    if (dot != std::string::npos) identifier.erase(dot, 1);
    std::ranges::transform(identifier, identifier.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    const auto name = getGameName(identifier);
    if (!name) return "PS2-Recomp | " + filename;
    return "PS2-Recomp | " + std::string(name) + " | " + filename;
}

[[noreturn]] void finish_game_process(int status) {
    // The upstream runner avoids runtime/static destructors after the EE stops.
    std::cout.flush();
    std::cerr.flush();
    std::_Exit(status);
}
}
