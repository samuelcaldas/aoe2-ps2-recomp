// Runner behavior adapted from the pinned PS2Recomp runner (GPL-3.0).
#include "game_entry.hpp"
#include <aoe2/runtime/deci2_runtime_session.hpp>
#include "ps2_runtime.h"
#ifdef _DEBUG
#include "ps2_log.h"
#endif

#include <iostream>
#include <stdexcept>

namespace {
// The game reads sectors by their real disc LBN, which only a disc image can
// resolve; the extracted CD root covers file-name lookups only.
void use_disc_image(const std::filesystem::path& image) {
    if (image.empty()) return;
    PS2Runtime::IoPaths paths = PS2Runtime::getIoPaths();
    paths.cdImage = image;
    PS2Runtime::setIoPaths(paths);
    std::cerr << "[main] disc image: " << image.string() << '\n';
}

[[noreturn]] void run_game(const std::filesystem::path& path, const std::filesystem::path& disc_image) {
    PS2Runtime runtime;
    aoe2::runtime::Deci2RuntimeSession tty(runtime);
    if (!runtime.initialize(aoe2::platform::game_window_title(path).c_str()))
        throw std::runtime_error("Failed to initialize PS2 runtime");
    tty.reset();
    if (!runtime.loadELF(path.string())) throw std::runtime_error("Failed to load ELF: " + path.string());
    use_disc_image(disc_image);
    runtime.run();
    tty.verify_output();
    tty.close();
#ifdef _DEBUG
    ps2_log::print_saved_location();
#endif
    aoe2::platform::finish_game_process(0);
}
}

int main(int count, char* arguments[]) {
    try {
        aoe2::platform::initialize_game_logging();
        run_game(aoe2::platform::game_path(count, arguments), aoe2::platform::disc_image_path(count, arguments));
    } catch (const std::exception& error) {
        std::cerr << "[main] fatal exception: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "[main] fatal exception: unknown\n";
    }
    aoe2::platform::finish_game_process(1);
}
