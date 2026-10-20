cmake_minimum_required(VERSION 3.20)
project(aoe2_ps2_recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-neutral interface library
add_library(aoe2_interfaces STATIC
    input/src/actions.cpp
    input/src/guest_mouse.cpp
    input/src/surface_mapping.cpp
    input/src/touch_gestures.cpp
    renderer/src/null_renderer.cpp
    runtime/src/guest_memory.cpp
)

target_include_directories(aoe2_interfaces PUBLIC
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

# Option to build desktop runtime probe with generated sources
option(AOE2_BUILD_DESKTOP_PROBE "Build the desktop compilation probe for generated code" OFF)

if(AOE2_BUILD_DESKTOP_PROBE)
    message(STATUS "Configuring desktop compilation probe...")

    set(GENERATED_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLES-50282")
    set(PS2X_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PS2Recomp")
    set(PS2X_RUNTIME_DIR "${PS2X_DIR}/ps2xRuntime")
    set(PS2X_IOP_DIR "${PS2X_DIR}/ps2xIOP")
    set(PS2X_RUNTIME_BUILD_DIR "${PS2X_DIR}/build-runtime")

    # Collect all generated C++ translation units (which includes generated register_functions.cpp)
    file(GLOB GENERATED_CPP_SOURCES CONFIGURE_DEPENDS
        "${GENERATED_SRC_DIR}/*.cpp"
    )

    list(LENGTH GENERATED_CPP_SOURCES GENERATED_COUNT)
    message(STATUS "Discovered ${GENERATED_COUNT} generated C++ translation units.")

    # Generated VU0 code may call SSE4.1 intrinsics; enable SSE4.1 only for the units that do,
    # detected by content so no generated file names are tracked here.
    set(AOE2_SSE41_INTRINSICS_REGEX "_mm_blendv_ps")
    set(SSE41_UNIT_COUNT 0)
    foreach(generated_source IN LISTS GENERATED_CPP_SOURCES)
        file(STRINGS "${generated_source}" sse41_match REGEX "${AOE2_SSE41_INTRINSICS_REGEX}" LIMIT_COUNT 1)
        if(sse41_match)
            set_source_files_properties("${generated_source}" PROPERTIES COMPILE_OPTIONS "-msse4.1")
            math(EXPR SSE41_UNIT_COUNT "${SSE41_UNIT_COUNT} + 1")
        endif()
    endforeach()
    message(STATUS "Enabled SSE4.1 for ${SSE41_UNIT_COUNT} generated translation units.")

    # We do NOT include the upstream runner placeholder ps2xRuntime/src/runner/register_functions.cpp
    # because generated/SLES-50282/register_functions.cpp provides the authoritative table.
    add_library(aoe2_generated_game_code OBJECT
        ${GENERATED_CPP_SOURCES}
    )

    target_include_directories(aoe2_generated_game_code PRIVATE
        "${GENERATED_SRC_DIR}"
        "${PS2X_RUNTIME_DIR}/include"
        "${PS2X_RUNTIME_DIR}/src/lib/Kernel"
        "${PS2X_IOP_DIR}/include"
    )

    # Minimal desktop runtime probe executable
    add_executable(desktop_runtime_probe
        platform/desktop/src/desktop_runtime_probe.cpp
        $<TARGET_OBJECTS:aoe2_generated_game_code>
    )

    target_include_directories(desktop_runtime_probe PRIVATE
        "${PS2X_RUNTIME_DIR}/include"
    )

    # Link built PS2Recomp runtime and required transitive libraries
    target_link_directories(desktop_runtime_probe PRIVATE
        "${PS2X_RUNTIME_BUILD_DIR}/ps2xRuntime"
        "${PS2X_RUNTIME_BUILD_DIR}/ps2xIOP"
        "${PS2X_RUNTIME_BUILD_DIR}/_deps/raylib-build/raylib"
    )

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET GLOBAL
        libavcodec
        libavformat
        libavutil
        libswresample
        libswscale
    )

    find_package(OpenGL REQUIRED)

    target_link_libraries(desktop_runtime_probe PRIVATE
        "${PS2X_RUNTIME_BUILD_DIR}/ps2xRuntime/libps2_runtime.a"
        "${PS2X_RUNTIME_BUILD_DIR}/ps2xIOP/libps2_iop.a"
        "${PS2X_RUNTIME_BUILD_DIR}/_deps/raylib-build/raylib/libraylib.a"
        PkgConfig::FFMPEG
        OpenGL::GL
        OpenGL::GLX
        m
        pthread
        rt
        dl
    )
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
