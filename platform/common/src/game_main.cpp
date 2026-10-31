#include <exception>
#include <string>
#if defined(__SWITCH__)
#include <switch.h>
extern "C" void switch_early_terminate(void) noexcept;
#endif
// Runner behavior adapted from the pinned PS2Recomp runner (GPL-3.0).
#include "game_entry.hpp"
#include <aoe2/runtime/deci2_runtime_session.hpp>
#include <aoe2/game_hooks/map_view_session.hpp>
#include "ps2_runtime.h"
#if defined(__SWITCH__)
#include <switch.h>
#include <aoe2/platform/switch/switch_platform.hpp>
#endif
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
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] run_game START\n", 29);
#endif
    PS2Runtime runtime;
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] PS2Runtime constructed\n", 37);
#endif
    aoe2::runtime::Deci2RuntimeSession tty(runtime);
    if (!runtime.initialize(aoe2::platform::game_window_title(path).c_str()))
        throw std::runtime_error("Failed to initialize PS2 runtime");
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] PS2Runtime initialized\n", 37);
#endif
    tty.reset();
    if (!runtime.loadELF(path.string())) throw std::runtime_error("Failed to load ELF: " + path.string());
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] PS2Runtime ELF loaded\n", 36);
#endif
    aoe2::game_hooks::MapViewSession map_view_session(runtime);
    use_disc_image(disc_image);
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] calling runtime.run()\n", 36);
#endif
    runtime.run();
    map_view_session.Close();
    tty.verify_output();
    tty.close();
#ifdef _DEBUG
    ps2_log::print_saved_location();
#endif
    aoe2::platform::finish_game_process(0);
}
}

int main(int count, char* arguments[]) {
#if defined(__SWITCH__)
    svcOutputDebugString("[aoe2-switch] Entering main() BEFORE EVERYTHING\n", 48);
    std::set_terminate(switch_early_terminate);
    aoe2::platform::switch_env::SwitchPlatform::Instance().Initialize(count, arguments);
    svcOutputDebugString("[aoe2-switch] SwitchPlatform initialized.\n", 42);
#endif
    try {
        aoe2::platform::initialize_game_logging();
        run_game(aoe2::platform::game_path(count, arguments), aoe2::platform::disc_image_path(count, arguments));
    } catch (const std::exception& error) {
        std::cerr << "[main] fatal exception: " << error.what() << '\n';
    } catch (...) {
        std::cerr << "[main] fatal exception: unknown\n";
    }
#if defined(__SWITCH__)
    aoe2::platform::switch_env::SwitchPlatform::Instance().Shutdown();
#endif
    aoe2::platform::finish_game_process(1);
}
