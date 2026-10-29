cmake_minimum_required(VERSION 3.20)
project(map_view_module_tests LANGUAGES CXX)
enable_testing()
get_filename_component(ROOT "../.." ABSOLUTE)
add_executable(map_view_module_tests map_view_module_tests.cpp
    ${ROOT}/game_hooks/src/map_view_state.cpp
    ${ROOT}/renderer/src/map_view_layout.cpp
    ${ROOT}/renderer/src/hud_layout.cpp)
target_compile_features(map_view_module_tests PRIVATE cxx_std_20)
target_include_directories(map_view_module_tests PRIVATE
    fakes
    ${ROOT}/game_hooks/src
    ${ROOT}/game_hooks/include
    ${ROOT}/renderer/include
    ${ROOT}/input/include)
add_test(NAME map_view_module_tests COMMAND map_view_module_tests)
add_executable(map_view_wrapper_tests wrapper_tests.cpp
    ${ROOT}/game_hooks/src/map_view_session.cpp
    ${ROOT}/game_hooks/src/map_view_observation.cpp
    ${ROOT}/game_hooks/src/map_view_module.cpp
    ${ROOT}/game_hooks/src/map_view_config.cpp
    ${ROOT}/game_hooks/src/map_view_state.cpp
    ${ROOT}/renderer/src/map_view_compositor.cpp
    ${ROOT}/renderer/src/map_view_layout.cpp
    ${ROOT}/renderer/src/hud_layout.cpp
    ${ROOT}/input/src/presentation_transform.cpp
    ${ROOT}/input/src/hud_inverse_mapper.cpp)
target_compile_features(map_view_wrapper_tests PRIVATE cxx_std_20)
target_compile_options(map_view_wrapper_tests PRIVATE -msse4.1)
target_include_directories(map_view_wrapper_tests PRIVATE fakes ${ROOT}/game_hooks/src
    ${ROOT}/game_hooks/include ${ROOT}/renderer/include ${ROOT}/input/include ${ROOT}/third_party/PS2Recomp/ps2xRuntime/include)
add_test(NAME map_view_wrapper_tests COMMAND map_view_wrapper_tests)
