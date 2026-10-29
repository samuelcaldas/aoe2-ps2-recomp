# Optional characterization of owned generated originals. No source is copied.
# Root registration (after aoe2_deci2_host):
# option(AOE2_BUILD_MAP_AREA_TESTS "Characterize generated map surfaces" OFF)
# if(AOE2_BUILD_MAP_AREA_TESTS)
#     include(cmake/Aoe2MapAreaTests.cmake)
# endif()
# Without root edits, configure with -DCMAKE_PROJECT_aoe2_ps2_recomp_INCLUDE=<this file>.
if(NOT TARGET aoe2_deci2_host)
    set_property(GLOBAL PROPERTY AOE2_MAP_AREA_MODULE "${CMAKE_CURRENT_LIST_FILE}")
    function(aoe2_deferred_map_area_tests)
        get_property(module GLOBAL PROPERTY AOE2_MAP_AREA_MODULE)
        include("${module}")
    endfunction()
    cmake_language(DEFER CALL aoe2_deferred_map_area_tests)
    return()
endif()
set(AOE2_MAP_AREA_CACHED_BUILD "" CACHE PATH "Compatible built desktop runtime")
if(NOT AOE2_MAP_AREA_CACHED_BUILD)
    message(FATAL_ERROR "Set AOE2_MAP_AREA_CACHED_BUILD to a compatible desktop runtime build")
endif()
find_package(PkgConfig REQUIRED)
pkg_check_modules(MAP_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libswscale libavutil)
set(map_upstream "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PS2Recomp")
set(map_originals)
foreach(address IN ITEMS 31a410 1ab7e0 1abac0 1ac110 1ac2e0 1ae350 1ae358 1ae38c 21b4c0 21b560 21acd0 21a680 21e350)
    file(GLOB unit "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLES-50282/*_0x${address}.cpp")
    list(LENGTH unit count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "Map contracts need exactly one generated original for ${address}")
    endif()
    list(APPEND map_originals ${unit})
endforeach()
add_executable(aoe2_map_area_contract_tests tests/cpp/map_area_contract_tests.cpp
    ${map_originals} "${map_upstream}/ps2xTest/src/test_function_table.cpp")
target_include_directories(aoe2_map_area_contract_tests PRIVATE generated/SLES-50282
    "${map_upstream}/ps2xRuntime/include" "${map_upstream}/ps2xRuntime/src/lib/Kernel"
    "${AOE2_MAP_AREA_CACHED_BUILD}/_deps/raylib-src/src" "${map_upstream}/ps2xIOP/include")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    target_compile_options(aoe2_map_area_contract_tests PRIVATE -msse4.1)
endif()
target_link_libraries(aoe2_map_area_contract_tests PRIVATE
    "${AOE2_MAP_AREA_CACHED_BUILD}/ps2recomp/ps2xRuntime/libps2_runtime.a"
    "${AOE2_MAP_AREA_CACHED_BUILD}/_deps/raylib-build/raylib/libraylib.a"
    m pthread GL GLX rt dl PkgConfig::MAP_FFMPEG
    "${AOE2_MAP_AREA_CACHED_BUILD}/ps2recomp/ps2xIOP/libps2_iop.a" aoe2_deci2_host)
add_test(NAME map_area_contracts COMMAND aoe2_map_area_contract_tests)
set_tests_properties(map_area_contracts PROPERTIES TIMEOUT 60)
