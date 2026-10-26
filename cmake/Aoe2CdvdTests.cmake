# Compile only the owned contract-test originals, never the complete generated function table.
set(cdvd_originals)
foreach(unit IN ITEMS FUN_00129b10_0x129b10.cpp FUN_0012a650_0x12a650.cpp
                      FUN_001295d0_0x1295d0.cpp FUN_00129588_0x129588.cpp
                      FUN_0012bc20_0x12bc20.cpp)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLES-50282/${unit}")
    if(NOT EXISTS "${source}")
        message(FATAL_ERROR "AOE2_BUILD_CDVD_RUNTIME_TESTS requires owned generated source: ${source}")
    endif()
    list(APPEND cdvd_originals "${source}")
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/Aoe2Runtime.cmake")
add_executable(aoe2_cdvd_media_tests tests/cpp/cdvd_media_tests.cpp
    tests/cpp/cdvd_completion_tests.cpp ${cdvd_originals}
    game_hooks/src/kcet_cdvd_module.cpp "${PS2X_DIR}/ps2xTest/src/test_function_table.cpp")
target_include_directories(aoe2_cdvd_media_tests PRIVATE
    game_hooks/src "${CMAKE_CURRENT_SOURCE_DIR}/generated/SLES-50282")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
    target_compile_options(aoe2_cdvd_media_tests PRIVATE -msse4.1)
endif()
target_link_libraries(aoe2_cdvd_media_tests PRIVATE aoe2_interfaces aoe2_ps2_runtime)
add_test(NAME cdvd_media COMMAND aoe2_cdvd_media_tests)
set_tests_properties(cdvd_media PROPERTIES TIMEOUT 30)
