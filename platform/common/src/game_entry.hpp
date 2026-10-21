#pragma once

#include <filesystem>
#include <string>

namespace aoe2::platform {
std::filesystem::path game_path(int count, char* arguments[]);
// Owned disc image from argv[2], or empty when none is given. Throws if the
// argument names a file that does not exist.
std::filesystem::path disc_image_path(int count, char* arguments[]);
std::string game_window_title(const std::filesystem::path& path);
void initialize_game_logging();
[[noreturn]] void finish_game_process(int status);
}
