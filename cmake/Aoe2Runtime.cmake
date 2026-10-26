include_guard(GLOBAL)

set(PS2X_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PS2Recomp")
set(PS2X_RUNTIME_DIR "${PS2X_DIR}/ps2xRuntime")
if(NOT EXISTS "${PS2X_RUNTIME_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PS2Recomp missing; run tools/bootstrap_dependencies.py")
endif()

if(NINTENDO_SWITCH)
    add_compile_definitions(_GNU_SOURCE MA_NO_RUNTIME_LINKING)
    set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
endif()
if(NOT MSVC AND NOT ANDROID AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    string(APPEND CMAKE_C_FLAGS " -march=x86-64-v2")
    string(APPEND CMAKE_CXX_FLAGS " -march=x86-64-v2")
endif()

option(AOE2_RUNTIME_DIAGNOSTICS "Verbose runtime traces and stop-on-missing-function" OFF)
option(AOE2_GAME_TTY "Enable the EE DECI2 TTY host" ON)
foreach(disabled IN ITEMS PS2X_BUILD_RECOMP PS2X_BUILD_ANALYZER PS2X_BUILD_TEST
                         PS2X_BUILD_STUDIO PS2X_ENABLE_DEBUG_UI PS2X_ENABLE_SCCACHE)
    set(${disabled} OFF CACHE BOOL "Disabled by aoe2_ps2_recomp" FORCE)
endforeach()
set(PS2X_ENABLE_AGRESSIVE_LOGS ${AOE2_RUNTIME_DIAGNOSTICS} CACHE BOOL "Runtime diagnostics" FORCE)
set(PS2X_ENABLE_IOP_RPC_TRACE ${AOE2_RUNTIME_DIAGNOSTICS} CACHE BOOL "IOP diagnostics" FORCE)
set(PS2X_BUILD_RUNTIME ON CACHE BOOL "Enabled by aoe2_ps2_recomp" FORCE)
add_subdirectory("${PS2X_DIR}" "${CMAKE_BINARY_DIR}/ps2recomp" EXCLUDE_FROM_ALL)

include("${CMAKE_CURRENT_LIST_DIR}/Aoe2Deci2.cmake")
aoe2_install_deci2(ps2_runtime "${PS2X_RUNTIME_DIR}/src/lib/Kernel/Syscalls/Deci2.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(aoe2_ps2_runtime INTERFACE)
target_link_libraries(aoe2_ps2_runtime INTERFACE ps2_runtime)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64|^arm|^ARM")
    include(FetchContent)
    FetchContent_GetProperties(sse2neon)
    if(NOT sse2neon_SOURCE_DIR)
        message(FATAL_ERROR "ARM target but PS2Recomp did not fetch sse2neon")
    endif()
    target_include_directories(aoe2_ps2_runtime INTERFACE "${sse2neon_SOURCE_DIR}")
    target_compile_definitions(aoe2_ps2_runtime INTERFACE USE_SSE2NEON)
endif()

set(AOE2_GAME_RUNNER_SOURCES
    platform/common/src/game_main.cpp
    platform/common/src/game_entry.cpp
    platform/common/src/game_logging.cpp)

if(AOE2_BUILD_DECI2_RUNTIME_TESTS OR AOE2_BUILD_DESKTOP_PROBE)
    add_executable(aoe2_overlay_dispatch_tests tests/cpp/overlay_dispatch_tests.cpp
        "${PS2X_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(aoe2_overlay_dispatch_tests PRIVATE aoe2_ps2_runtime)
    add_test(NAME overlay_dispatch COMMAND aoe2_overlay_dispatch_tests)
    set_tests_properties(overlay_dispatch PROPERTIES TIMEOUT 30)
endif()

if(AOE2_BUILD_DECI2_RUNTIME_TESTS)
    add_executable(aoe2_deci2_runtime_tests tests/cpp/deci2_runtime_tests.cpp
        tests/cpp/deci2_session_tests.cpp
        tests/cpp/deci2_io_tests.cpp
        "${PS2X_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(aoe2_deci2_runtime_tests PRIVATE aoe2_ps2_runtime)
    add_test(NAME deci2_runtime COMMAND aoe2_deci2_runtime_tests)
    set_tests_properties(deci2_runtime PROPERTIES TIMEOUT 30)
    add_executable(aoe2_deci2_runner_tests ${AOE2_GAME_RUNNER_SOURCES}
        "${PS2X_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(aoe2_deci2_runner_tests PRIVATE aoe2_ps2_runtime)
    add_test(NAME deci2_runner_usage COMMAND aoe2_deci2_runner_tests)
    set_tests_properties(deci2_runner_usage PROPERTIES WILL_FAIL TRUE TIMEOUT 10)
endif()
