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
    renderer/src/null_renderer.cpp
    runtime/src/guest_memory.cpp
)

target_include_directories(aoe2_interfaces PUBLIC
    assets/include
    core/include
    input/include
    renderer/include
    runtime/include
    platform/desktop/include
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
