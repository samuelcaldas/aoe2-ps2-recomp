# Focused generated-code differential tests. No generated source is copied.
set(AOE2_GPU_CACHED_BUILD "" CACHE PATH "Existing compatible desktop runtime build (optional)")
set(gpu_upstream "${CMAKE_CURRENT_SOURCE_DIR}/third_party/PS2Recomp")
if(AOE2_GPU_CACHED_BUILD)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GPU_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libswscale libavutil)
    add_library(aoe2_gpu_test_runtime INTERFACE)
    target_include_directories(aoe2_gpu_test_runtime INTERFACE
        "${gpu_upstream}/ps2xRuntime/include" "${gpu_upstream}/ps2xRuntime/src/lib/Kernel"
        "${AOE2_GPU_CACHED_BUILD}/_deps/raylib-src/src" "${gpu_upstream}/ps2xIOP/include")
    target_link_libraries(aoe2_gpu_test_runtime INTERFACE
        "${AOE2_GPU_CACHED_BUILD}/ps2recomp/ps2xRuntime/libps2_runtime.a"
        "${AOE2_GPU_CACHED_BUILD}/_deps/raylib-build/raylib/libraylib.a"
        m pthread GL GLX rt dl PkgConfig::GPU_FFMPEG
        "${AOE2_GPU_CACHED_BUILD}/ps2recomp/ps2xIOP/libps2_iop.a" aoe2_deci2_host)
else()
    include("${CMAKE_CURRENT_LIST_DIR}/Aoe2Runtime.cmake")
    add_library(aoe2_gpu_test_runtime INTERFACE)
    target_link_libraries(aoe2_gpu_test_runtime INTERFACE aoe2_ps2_runtime)
endif()
set(gpu_originals)
foreach(address IN ITEMS 125808 125858 1258a8 1258f8 125940 125988 1259c8 1043d8 104448 1045e8 104600 1046a8 123a70)
    file(GLOB unit "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLES-50282/*_0x${address}.cpp")
    list(LENGTH unit count)
    if(NOT count EQUAL 1)
        message(FATAL_ERROR "GPU differential test needs exactly one generated unit for ${address}")
    endif()
    list(APPEND gpu_originals ${unit})
endforeach()
add_executable(aoe2_gpu_differential_tests tests/cpp/gpu_differential_tests.cpp
    ${gpu_originals} "${gpu_upstream}/ps2xTest/src/test_function_table.cpp"
    game_hooks/src/kcet_gpu_module.cpp)
target_include_directories(aoe2_gpu_differential_tests PRIVATE game_hooks/src generated/SLES-50282)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    target_compile_options(aoe2_gpu_differential_tests PRIVATE -msse4.1)
endif()
target_link_libraries(aoe2_gpu_differential_tests PRIVATE aoe2_interfaces aoe2_gpu_test_runtime)
add_test(NAME gpu_differential COMMAND aoe2_gpu_differential_tests)
set_tests_properties(gpu_differential PROPERTIES TIMEOUT 60)
