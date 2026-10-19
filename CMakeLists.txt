cmake_minimum_required(VERSION 3.20)
project(aoe2_ps2_recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-neutral interface library
add_library(aoe2_interfaces STATIC
    input/src/actions.cpp
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
    set(PS2X_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PS2Recomp/ps2xRuntime")
    set(PS2X_IOP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PS2Recomp/ps2xIOP")

    # Collect all generated C++ translation units without omitting any
    file(GLOB GENERATED_CPP_SOURCES CONFIGURE_DEPENDS
        "${GENERATED_SRC_DIR}/*.cpp"
    )

    list(LENGTH GENERATED_CPP_SOURCES GENERATED_COUNT)
    message(STATUS "Discovered ${GENERATED_COUNT} generated C++ translation units.")

    add_library(aoe2_generated_game_code OBJECT
        ${GENERATED_CPP_SOURCES}
        "${PS2X_RUNTIME_DIR}/src/runner/register_functions.cpp"
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
endif()

# Focused C++ interface link and behavior test
enable_testing()
add_test(NAME smoke_test COMMAND aoe2_smoke_test)
