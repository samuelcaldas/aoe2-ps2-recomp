cmake_minimum_required(VERSION 3.20)
project(aoe2_ps2_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-neutral interface library
add_library(aoe2_interfaces STATIC
    assets/src/asset_path_resolver.cpp
    assets/src/loose_hd_asset_provider.cpp
    input/src/actions.cpp
    input/src/guest_keyboard.cpp
    input/src/guest_mouse.cpp
    input/src/input_routing.cpp
    input/src/surface_mapping.cpp
    input/src/touch_gestures.cpp
    input/src/usb_mouse.cpp
    input/src/usb_mouse_rpc.cpp
    input/src/usb_keyboard_rpc.cpp
    game_hooks/src/kcet_hook_config.cpp
    game_hooks/src/native_module_switches.cpp
    kernel/src/vsync_model.cpp
    kernel/src/thread_model.cpp
    pad/src/pad_report.cpp
    pad/src/pad_device.cpp
    net/src/ilink_address.cpp
    net/src/ilink_socket_model.cpp
    net/src/in_memory_hub.cpp
    net/src/null_transport.cpp
    renderer/src/null_renderer.cpp
    runtime/src/guest_memory.cpp
)

target_include_directories(aoe2_interfaces PUBLIC
    assets/include
    core/include
    input/include
    net/include
    renderer/include
    runtime/include
    platform/desktop/include
    game_hooks/include
    kernel/include
    pad/include
    tests/cpp
)

# Desktop host smoke test executable
add_executable(aoe2_smoke_test
    platform/desktop/src/main.cpp
)

target_link_libraries(aoe2_smoke_test PRIVATE aoe2_interfaces)

enable_testing()
add_library(aoe2_deci2_host STATIC runtime/src/deci2_host.cpp runtime/src/deci2_tty_packet.cpp)
target_include_directories(aoe2_deci2_host PUBLIC runtime/include)
add_executable(aoe2_deci2_host_tests tests/cpp/deci2_host_tests.cpp)
target_link_libraries(aoe2_deci2_host_tests PRIVATE aoe2_deci2_host)
add_test(NAME deci2_host COMMAND aoe2_deci2_host_tests)
option(AOE2_BUILD_DECI2_RUNTIME_TESTS "Build DECI2 scheduler tests without generated game code" OFF)
if(AOE2_BUILD_DECI2_RUNTIME_TESTS)
    include(cmake/Aoe2Runtime.cmake)
endif()

# Recompiled game targets (generated code + PS2Recomp runtime built from source)
option(AOE2_BUILD_DESKTOP_PROBE "Build the headless runtime-initialization probe for generated code" OFF)
option(AOE2_BUILD_GAME "Build the playable recompiled game runner (aoe2_game)" OFF)
set(AOE2_DEFAULT_BOOT_ELF "" CACHE STRING "Boot ELF path baked into aoe2_game when no argument is given (required on Android/Switch)")

if(AOE2_BUILD_DESKTOP_PROBE OR AOE2_BUILD_GAME)
    enable_testing()
    include(cmake/Aoe2Recompiled.cmake)
endif()

# Focused C++ interface link and behavior test
enable_testing()
add_test(NAME smoke_test COMMAND aoe2_smoke_test)

# Touch-to-guest-mouse input translation tests
add_executable(aoe2_input_tests
    tests/cpp/input_tests.cpp
)

target_link_libraries(aoe2_input_tests PRIVATE aoe2_interfaces)
add_test(NAME input_tests COMMAND aoe2_input_tests)

# Decoupled HD asset provider tests
add_executable(aoe2_asset_tests
    tests/cpp/asset_tests.cpp
)

target_link_libraries(aoe2_asset_tests PRIVATE aoe2_interfaces)
add_test(NAME asset_tests COMMAND aoe2_asset_tests)

# Strict host-only KCET hook configuration parser tests
add_executable(aoe2_kcet_hook_config_tests tests/cpp/kcet_hook_config_tests.cpp)
target_link_libraries(aoe2_kcet_hook_config_tests PRIVATE aoe2_interfaces)
add_test(NAME kcet_hook_config_tests COMMAND aoe2_kcet_hook_config_tests)

add_executable(aoe2_native_module_switches_tests tests/cpp/native_module_switches_tests.cpp)
target_link_libraries(aoe2_native_module_switches_tests PRIVATE aoe2_interfaces)
add_test(NAME native_module_switches COMMAND aoe2_native_module_switches_tests)

add_executable(aoe2_vsync_model_tests tests/cpp/vsync_model_tests.cpp)
target_link_libraries(aoe2_vsync_model_tests PRIVATE aoe2_interfaces)
add_test(NAME vsync_model COMMAND aoe2_vsync_model_tests)

add_executable(aoe2_thread_model_tests tests/cpp/thread_model_tests.cpp)
target_link_libraries(aoe2_thread_model_tests PRIVATE aoe2_interfaces)
add_test(NAME thread_model COMMAND aoe2_thread_model_tests)

add_executable(aoe2_pad_report_tests tests/cpp/pad_report_tests.cpp)
target_link_libraries(aoe2_pad_report_tests PRIVATE aoe2_interfaces)
add_test(NAME pad_report COMMAND aoe2_pad_report_tests)
add_executable(aoe2_pad_device_tests tests/cpp/pad_device_tests.cpp)
target_link_libraries(aoe2_pad_device_tests PRIVATE aoe2_interfaces)
add_test(NAME pad_device COMMAND aoe2_pad_device_tests)

# i.LINK socket model tests (in-memory network)
add_executable(aoe2_net_tests tests/cpp/net_tests.cpp)
target_link_libraries(aoe2_net_tests PRIVATE aoe2_interfaces)
add_test(NAME net_tests COMMAND aoe2_net_tests)

# POSIX UDP backend for the i.LINK socket model (not available on Switch)
if(UNIX AND NOT NINTENDO_SWITCH)
    add_library(aoe2_udp_transport STATIC platform/desktop/src/udp_transport.cpp)
    target_link_libraries(aoe2_udp_transport PUBLIC aoe2_interfaces)
    add_executable(aoe2_udp_transport_tests tests/cpp/udp_transport_tests.cpp)
    target_link_libraries(aoe2_udp_transport_tests PRIVATE aoe2_udp_transport)
    add_test(NAME udp_transport_tests COMMAND aoe2_udp_transport_tests)
endif()
