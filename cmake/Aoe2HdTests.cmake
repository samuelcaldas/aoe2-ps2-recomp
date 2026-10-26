# Focused generated-code differential tests. No generated source is copied.
set(AOE2_HD_CACHED_BUILD "" CACHE PATH "Existing compatible desktop runtime build (optional)")
set(AOE2_HD_ORIGINAL_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" CACHE PATH "Owned generated sources and pinned dependencies")
set(hd_upstream "${AOE2_HD_ORIGINAL_ROOT}/third_party/PS2Recomp")
if(AOE2_HD_CACHED_BUILD)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HD_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libswscale libavutil)
    add_library(aoe2_hd_test_runtime INTERFACE)
    target_include_directories(aoe2_hd_test_runtime INTERFACE
        "${hd_upstream}/ps2xRuntime/include" "${hd_upstream}/ps2xRuntime/src/lib/Kernel"
        "${AOE2_HD_CACHED_BUILD}/_deps/raylib-src/src" "${hd_upstream}/ps2xIOP/include")
    target_link_libraries(aoe2_hd_test_runtime INTERFACE
        "${AOE2_HD_CACHED_BUILD}/ps2recomp/ps2xRuntime/libps2_runtime.a"
        "${AOE2_HD_CACHED_BUILD}/_deps/raylib-build/raylib/libraylib.a"
        m pthread GL GLX rt dl PkgConfig::HD_FFMPEG
        "${AOE2_HD_CACHED_BUILD}/ps2recomp/ps2xIOP/libps2_iop.a" aoe2_deci2_host)
else()
    include("${CMAKE_CURRENT_LIST_DIR}/Aoe2Runtime.cmake")
    add_library(aoe2_hd_test_runtime INTERFACE)
    target_link_libraries(aoe2_hd_test_runtime INTERFACE aoe2_ps2_runtime)
endif()
set(hd_originals)
foreach(address IN ITEMS 129560 129428 1294c0)
    file(GLOB unit "${AOE2_HD_ORIGINAL_ROOT}/generated/SLES-50282/*_0x${address}.cpp")
    list(LENGTH unit count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "HD differential needs exactly one generated unit for ${address}")
    endif()
    list(APPEND hd_originals ${unit})
endforeach()
add_executable(aoe2_hd_runtime_tests tests/cpp/hd_runtime_tests.cpp ${hd_originals}
    "${hd_upstream}/ps2xTest/src/test_function_table.cpp" game_hooks/src/kcet_hd_module.cpp)
target_include_directories(aoe2_hd_runtime_tests PRIVATE game_hooks/src "${AOE2_HD_ORIGINAL_ROOT}/generated/SLES-50282")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    target_compile_options(aoe2_hd_runtime_tests PRIVATE -msse4.1)
endif()
target_link_libraries(aoe2_hd_runtime_tests PRIVATE aoe2_interfaces aoe2_hd_test_runtime)
add_test(NAME hd_runtime_differential COMMAND aoe2_hd_runtime_tests)
set_tests_properties(hd_runtime_differential PROPERTIES TIMEOUT 60)
