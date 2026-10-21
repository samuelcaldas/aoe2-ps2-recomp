#pragma once

#include <filesystem>
#include <string>

namespace aoe2::platform {
std::filesystem::path game_path(int count, char* arguments[]);
std::string game_window_title(const std::filesystem::path& path);
void initialize_game_logging();
[[noreturn]] void finish_game_process(int status);
}
