# Recompiled game targets: the generated code linked against the pinned
# PS2Recomp runtime, which is built from source here so the same build works
# on desktop, Android, and Switch toolchains.
#
#   aoe2_generated_game_code  OBJECT library with every generated unit
#   desktop_runtime_probe     headless init check; never runs guest code
#   aoe2_game                 playable runner (executable, or a shared
#                             library loaded by NativeActivity on Android)

set(GENERATED_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLES-50282")
set(PS2X_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PS2Recomp")
set(PS2X_RUNTIME_DIR "${PS2X_DIR}/ps2xRuntime")
if(NINTENDO_SWITCH)
    # devkitA64 newlib requires _GNU_SOURCE for posix_memalign and other POSIX APIs.
    add_compile_definitions(_GNU_SOURCE MA_NO_RUNTIME_LINKING)
    # Raylib has no Switch audio driver; disable raudio and rely on switch_compat.c stubs
    set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
endif()

set(AOE2_GENERATED_UNITY_BATCH 16 CACHE STRING "Generated units per unity translation unit (0 disables unity builds)")
set(AOE2_GENERATED_OPTIMIZATION "-O1" CACHE STRING "Optimization flag for generated code; higher levels multiply compile time")

if(NOT EXISTS "${PS2X_RUNTIME_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "PS2Recomp missing at ${PS2X_DIR}; run tools/bootstrap_dependencies.py")
endif()

# Desktop x86-64 baseline: the runtime and the generated VU0 code use SSE4.1
# intrinsics, which GCC/Clang only inline when the target ISA includes them.
# Android x86_64 already guarantees SSE4.2 through its ABI.
if(NOT MSVC AND NOT ANDROID AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    string(APPEND CMAKE_C_FLAGS " -march=x86-64-v2")
    string(APPEND CMAKE_CXX_FLAGS " -march=x86-64-v2")
endif()

# Runtime only: no recompiler, analyzer, tests, studio, debug UI, or sccache.
foreach(ps2x_disabled IN ITEMS PS2X_BUILD_RECOMP PS2X_BUILD_ANALYZER PS2X_BUILD_TEST PS2X_BUILD_STUDIO
                               PS2X_ENABLE_DEBUG_UI PS2X_ENABLE_SCCACHE PS2X_ENABLE_AGRESSIVE_LOGS)
    set(${ps2x_disabled} OFF CACHE BOOL "Disabled by aoe2_ps2_recomp" FORCE)
endforeach()
set(PS2X_BUILD_RUNTIME ON CACHE BOOL "Enabled by aoe2_ps2_recomp" FORCE)
add_subdirectory("${PS2X_DIR}" "${CMAKE_BINARY_DIR}/ps2recomp" EXCLUDE_FROM_ALL)

file(GLOB GENERATED_CPP_SOURCES CONFIGURE_DEPENDS "${GENERATED_SRC_DIR}/*.cpp")
list(LENGTH GENERATED_CPP_SOURCES GENERATED_COUNT)
if(GENERATED_COUNT EQUAL 0)
    message(FATAL_ERROR "No generated sources in ${GENERATED_SRC_DIR}; run tools/run_ps2recomp_pipeline.py")
endif()
message(STATUS "Discovered ${GENERATED_COUNT} generated C++ translation units.")

# register_functions.cpp contains 58k sequential assignment statements. Compiling
# it inside a unity batch under -O1/-O2 causes GCC optimization passes to take hours.
# Compiling it standalone under -O0 takes ~20 seconds.
set_source_files_properties("${GENERATED_SRC_DIR}/register_functions.cpp" PROPERTIES
    SKIP_UNITY_BUILD_INCLUSION TRUE
    COMPILE_OPTIONS "-O0"
)

# Everything that includes ps2_runtime.h links this: the runtime plus the
# per-architecture definitions the upstream only sets in its own directory.
add_library(aoe2_ps2_runtime INTERFACE)
target_link_libraries(aoe2_ps2_runtime INTERFACE ps2_runtime)

add_library(aoe2_generated_game_code OBJECT ${GENERATED_CPP_SOURCES})
target_include_directories(aoe2_generated_game_code PRIVATE "${GENERATED_SRC_DIR}")
target_link_libraries(aoe2_generated_game_code PRIVATE aoe2_ps2_runtime)
if(NOT MSVC AND AOE2_GENERATED_OPTIMIZATION)
    target_compile_options(aoe2_generated_game_code PRIVATE "${AOE2_GENERATED_OPTIMIZATION}")
endif()

if(AOE2_GENERATED_UNITY_BATCH GREATER 0)
    set_target_properties(aoe2_generated_game_code PROPERTIES
        UNITY_BUILD ON
        UNITY_BUILD_BATCH_SIZE "${AOE2_GENERATED_UNITY_BATCH}"
    )
endif()

# The upstream root scopes USE_SSE2NEON and the sse2neon include to its own
# directory; generated code includes ps2_runtime.h and needs them as well.
# PS2X_IS_ARM_TARGET is set in the upstream directory scope, so detect ARM here.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64|^arm|^ARM")
    include(FetchContent)
    FetchContent_GetProperties(sse2neon)
    if(NOT sse2neon_SOURCE_DIR)
        message(FATAL_ERROR "ARM target but PS2Recomp did not fetch sse2neon")
    endif()
    target_include_directories(aoe2_ps2_runtime INTERFACE "${sse2neon_SOURCE_DIR}")
    target_compile_definitions(aoe2_ps2_runtime INTERFACE USE_SSE2NEON)
endif()

function(aoe2_link_recompiled target)
    target_sources(${target} PRIVATE $<TARGET_OBJECTS:aoe2_generated_game_code>)
    target_link_libraries(${target} PRIVATE aoe2_ps2_runtime)
endfunction()

if(AOE2_BUILD_DESKTOP_PROBE)
    add_executable(desktop_runtime_probe platform/desktop/src/desktop_runtime_probe.cpp)
    aoe2_link_recompiled(desktop_runtime_probe)

    # Headless runtime initialization: loads the owned ELF and resolves the entry
    # point against the generated table without executing any guest code.
    add_test(NAME desktop_runtime_probe_init
        COMMAND desktop_runtime_probe
            --elf "${CMAKE_CURRENT_SOURCE_DIR}/game/SLES-50282/AOK.ELF"
            --function-map "${CMAKE_CURRENT_SOURCE_DIR}/analysis/generated/function_map.csv"
    )
    set_tests_properties(desktop_runtime_probe_init PROPERTIES
        PASS_REGULAR_EXPRESSION "runtime initialized: entry point resolved; guest code not executed"
        FAIL_REGULAR_EXPRESSION "\\[probe\\] error"
        TIMEOUT 120
    )
endif()

if(AOE2_BUILD_GAME)
    # The upstream runner entry point, compiled unchanged.
    set(AOE2_RUNNER_MAIN "${PS2X_RUNTIME_DIR}/src/main.cpp")
    if(ANDROID)
        add_library(aoe2_game SHARED "${AOE2_RUNNER_MAIN}")
        target_link_options(aoe2_game PRIVATE "-Wl,--undefined=ANativeActivity_onCreate")
    else()
        add_executable(aoe2_game "${AOE2_RUNNER_MAIN}")
    endif()
    aoe2_link_recompiled(aoe2_game)
    # Compiled into the executable (not a static library) so the self-registering
    # game override is never dropped by the linker.
    target_sources(aoe2_game PRIVATE game_hooks/src/kcet_mouse_overrides.cpp)
    if(NINTENDO_SWITCH)
        target_sources(aoe2_game PRIVATE platform/switch/src/switch_compat.c)
    endif()
    target_link_libraries(aoe2_game PRIVATE aoe2_interfaces)
    if(AOE2_DEFAULT_BOOT_ELF)
        target_compile_definitions(aoe2_game PRIVATE PS2X_DEFAULT_BOOT_ELF="${AOE2_DEFAULT_BOOT_ELF}")
    endif()

    if(NINTENDO_SWITCH)
        # Homebrew package; the runtime boots AOK.ELF and reads the disc from the
        # NRO's directory (sdmc:/switch/aoe2/), so the boot path stays relative.
        nx_generate_nacp(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/aoe2_game.nacp"
            NAME "AoE II Recomp" AUTHOR "aoe2-ps2-recomp" VERSION "0.1.0")
        nx_create_nro(aoe2_game NACP "${CMAKE_CURRENT_BINARY_DIR}/aoe2_game.nacp")
    endif()
endif()
