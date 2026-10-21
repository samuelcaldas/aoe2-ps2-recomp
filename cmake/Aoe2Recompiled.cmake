# Recompiled game targets: the generated code linked against the pinned
# PS2Recomp runtime, which is built from source here so the same build works
# on desktop, Android, and Switch toolchains.
#
#   aoe2_generated_game_code  OBJECT library with every generated unit
#   desktop_runtime_probe     headless init check; never runs guest code
#   aoe2_game                 playable runner (executable, or a shared
#                             library loaded by NativeActivity on Android)

set(GENERATED_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLES-50282")
include("${CMAKE_CURRENT_LIST_DIR}/Aoe2Runtime.cmake")

set(AOE2_GENERATED_UNITY_BATCH 16 CACHE STRING "Generated units per unity translation unit (0 disables unity builds)")
set(AOE2_GENERATED_OPTIMIZATION "-O1" CACHE STRING "Optimization flag for generated code; higher levels multiply compile time")

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
    set(AOE2_RUNNER_MAIN ${AOE2_GAME_RUNNER_SOURCES})
    if(ANDROID)
        add_library(aoe2_game SHARED ${AOE2_RUNNER_MAIN})
        target_link_options(aoe2_game PRIVATE "-Wl,--undefined=ANativeActivity_onCreate")
    else()
        add_executable(aoe2_game ${AOE2_RUNNER_MAIN})
    endif()
    aoe2_link_recompiled(aoe2_game)
    # Compiled into the executable (not a static library) so the self-registering
    # game override is never dropped by the linker.
    target_sources(aoe2_game PRIVATE
        game_hooks/src/kcet_hook_registry.cpp
        game_hooks/src/kcet_mouse_overrides.cpp
        game_hooks/src/kcet_keyboard.cpp
    )
    if(AOE2_RUNTIME_DIAGNOSTICS)
        target_sources(aoe2_game PRIVATE game_hooks/src/aoe2_diagnostics.cpp)
    endif()
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
