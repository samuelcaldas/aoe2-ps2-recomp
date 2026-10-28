#include "aoe2/platform/switch/switch_platform.hpp"
#include "aoe2/platform/switch/switch_input.hpp"

#include <cassert>
#include <cmath>
#include <iostream>

using namespace aoe2::platform::switch_env;

static void test_sdmc_path_resolution() {
    auto& platform = SwitchPlatform::Instance();
    platform.SetSdmcRoot("sdmc:/switch/aoe2");

    auto aok = platform.ResolveStoragePath("AOK.ELF");
    assert(aok.generic_string() == "sdmc:/switch/aoe2/AOK.ELF");

    auto iso = platform.ResolveStoragePath("aoe2.iso");
    assert(iso.generic_string() == "sdmc:/switch/aoe2/aoe2.iso");

    auto nested = platform.ResolveStoragePath("saves/mc0/save.dat");
    assert(nested.generic_string() == "sdmc:/switch/aoe2/saves/mc0/save.dat");

    std::cout << "[PASS] test_sdmc_path_resolution\n";
}

static void test_memory_status_check() {
    auto& platform = SwitchPlatform::Instance();
    auto status = platform.CheckMemoryStatus();

    // On host test, mock reports application mode
    assert(status.is_title_takeover == true);
    assert(status.available_memory_bytes > 1024ULL * 1024ULL * 1024ULL);

    std::cout << "[PASS] test_memory_status_check\n";
}

static void test_operation_mode_callbacks() {
    auto& platform = SwitchPlatform::Instance();
    bool callback_fired = false;
    ConsoleOperationMode received_mode = ConsoleOperationMode::Handheld;

    platform.RegisterOperationModeCallback([&](ConsoleOperationMode mode) {
        callback_fired = true;
        received_mode = mode;
    });

    platform.SetOperationMode(ConsoleOperationMode::Docked);
    assert(callback_fired);
    assert(received_mode == ConsoleOperationMode::Docked);
    assert(platform.GetOperationMode() == ConsoleOperationMode::Docked);

    std::cout << "[PASS] test_operation_mode_callbacks\n";
}

static void test_button_mapping() {
    // Test D-Pad
    uint16_t dpad = SwitchInputBridge::MapButtonsToDualShock2(switch_btn::DUP | switch_btn::DLEFT);
    assert((dpad & ps2_pad::UP) != 0);
    assert((dpad & ps2_pad::LEFT) != 0);
    assert((dpad & ps2_pad::DOWN) == 0);

    // Test Face buttons
    uint16_t face = SwitchInputBridge::MapButtonsToDualShock2(switch_btn::A | switch_btn::B | switch_btn::X | switch_btn::Y);
    assert((face & ps2_pad::CIRCLE) != 0);
    assert((face & ps2_pad::CROSS) != 0);
    assert((face & ps2_pad::TRIANGLE) != 0);
    assert((face & ps2_pad::SQUARE) != 0);

    // Test Triggers and Menu
    uint16_t extra = SwitchInputBridge::MapButtonsToDualShock2(switch_btn::ZL | switch_btn::ZR | switch_btn::PLUS | switch_btn::MINUS);
    assert((extra & ps2_pad::L2) != 0);
    assert((extra & ps2_pad::R2) != 0);
    assert((extra & ps2_pad::START) != 0);
    assert((extra & ps2_pad::SELECT) != 0);

    std::cout << "[PASS] test_button_mapping\n";
}

static void test_touch_coordinate_mapping() {
    SwitchInputBridge bridge;

    // Handheld 1280x720: center touch (640, 360) maps to guest 640x480 center (320, 240)
    auto center_handheld = bridge.MapTouchToGuest(640.0f, 360.0f, ConsoleOperationMode::Handheld);
    assert(std::fabs(center_handheld.x - 320.0f) < 1.0f);
    assert(std::fabs(center_handheld.y - 240.0f) < 1.0f);

    // Docked 1920x1080: center touch (960, 540) maps to guest 640x480 center (320, 240)
    auto center_docked = bridge.MapTouchToGuest(960.0f, 540.0f, ConsoleOperationMode::Docked);
    assert(std::fabs(center_docked.x - 320.0f) < 1.0f);
    assert(std::fabs(center_docked.y - 240.0f) < 1.0f);

    std::cout << "[PASS] test_touch_coordinate_mapping\n";
}

int main() {
    test_sdmc_path_resolution();
    test_memory_status_check();
    test_operation_mode_callbacks();
    test_button_mapping();
    test_touch_coordinate_mapping();
    std::cout << "All Switch platform tests passed successfully!\n";
    return 0;
}
