cmake_minimum_required(VERSION 3.20)
project(aoe2_ps2_recomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Platform-neutral interface library
add_library(aoe2_interfaces STATIC
    assets/src/asset_path_resolver.cpp
    assets/src/loose_hd_asset_provider.cpp
    assets/src/jasc_palette.cpp
    assets/src/slp_decoder.cpp
    assets/src/asset_resolver.cpp
    input/src/actions.cpp
    input/src/guest_keyboard.cpp
    input/src/guest_mouse.cpp
    input/src/input_routing.cpp
    input/src/presentation_transform.cpp
    input/src/surface_mapping.cpp
    input/src/touch_gestures.cpp
    input/src/usb_mouse.cpp
    input/src/usb_mouse_rpc.cpp
    input/src/usb_keyboard_rpc.cpp
    game_hooks/src/kcet_hook_config.cpp
    game_hooks/src/native_module_switches.cpp
    kernel/src/vsync_model.cpp
    kernel/src/thread_model.cpp
    keyboard/src/kt_keybd_leaf.cpp
    keyboard/src/kt_keybd_mid.cpp
    keyboard/src/kt_keybd_top.cpp
    mouse/src/kt_mouse_leaf.cpp
    mouse/src/kt_mouse_mid.cpp
    mouse/src/kt_mouse_top.cpp
    file/src/kt_file_model.cpp
    cdvd/src/cdvd_model.cpp
    mc/src/kt_mc_model.cpp
    mc/src/mc_storage_model.cpp
    gpu/src/kt_gpu_model.cpp
    hd/src/kt_hd_model.cpp
    hd/src/hd_storage_model.cpp
    pad/src/pad_report.cpp
    pad/src/kt_libpad_math.cpp
    pad/src/kt_libpad_model.cpp
    pad/src/kt_libpad_state.cpp
    pad/src/pad_device.cpp
    pad/src/host_button_map.cpp
    net/src/ilink_address.cpp
    net/src/ilink_socket_model.cpp
    net/src/in_memory_hub.cpp
    net/src/lan_discovery.cpp
    net/src/null_transport.cpp
    renderer/src/null_renderer.cpp
    renderer/src/gles_renderer.cpp
    renderer/src/hud_layout.cpp
    input/src/hud_inverse_mapper.cpp
    runtime/src/guest_memory.cpp
)

target_include_directories(aoe2_interfaces PUBLIC
    assets/include
    core/include
    input/include
    net/include
    renderer/include
    runtime/include
    platform/desktop/include
    game_hooks/include
    kernel/include
    pad/include
    mouse/include
    keyboard/include
    file/include
    cdvd/include
    mc/include
    hd/include
    gpu/include
    tests/cpp
)

add_executable(aoe2_kt_mouse_model_tests tests/cpp/kt_mouse_model_tests.cpp)
target_link_libraries(aoe2_kt_mouse_model_tests PRIVATE aoe2_interfaces)
add_test(NAME kt_mouse_model COMMAND aoe2_kt_mouse_model_tests)

add_executable(aoe2_kt_keybd_model_tests
    tests/cpp/kt_keybd_model_tests.cpp
    tests/cpp/kt_keybd_processing_tests.cpp
)
target_link_libraries(aoe2_kt_keybd_model_tests PRIVATE aoe2_interfaces)
add_test(NAME kt_keybd_model COMMAND aoe2_kt_keybd_model_tests)

# Desktop host smoke test executable
add_executable(aoe2_smoke_test
    platform/desktop/src/main.cpp
)

target_link_libraries(aoe2_smoke_test PRIVATE aoe2_interfaces)

enable_testing()
add_library(aoe2_deci2_host STATIC runtime/src/deci2_host.cpp runtime/src/deci2_tty_packet.cpp)
target_include_directories(aoe2_deci2_host PUBLIC runtime/include)
add_executable(aoe2_deci2_host_tests tests/cpp/deci2_host_tests.cpp)
target_link_libraries(aoe2_deci2_host_tests PRIVATE aoe2_deci2_host)
add_test(NAME deci2_host COMMAND aoe2_deci2_host_tests)
option(AOE2_BUILD_CDVD_RUNTIME_TESTS "Test media contract against owned generated originals" OFF)
if(AOE2_BUILD_CDVD_RUNTIME_TESTS)
    include(cmake/Aoe2CdvdTests.cmake)
endif()
option(AOE2_BUILD_DECI2_RUNTIME_TESTS "Build DECI2 scheduler tests without generated game code" OFF)
if(AOE2_BUILD_DECI2_RUNTIME_TESTS)
    include(cmake/Aoe2Runtime.cmake)
endif()

# Recompiled game targets (generated code + PS2Recomp runtime built from source)
option(AOE2_BUILD_DESKTOP_PROBE "Build the headless runtime-initialization probe for generated code" OFF)
option(AOE2_BUILD_GAME "Build the playable recompiled game runner (aoe2_game)" OFF)
set(AOE2_DEFAULT_BOOT_ELF "" CACHE STRING "Boot ELF path baked into aoe2_game when no argument is given (required on Android/Switch)")

if(AOE2_BUILD_DESKTOP_PROBE OR AOE2_BUILD_GAME)
    enable_testing()
    include(cmake/Aoe2Recompiled.cmake)
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

# Shared window <-> GS frame <-> logical screen presentation transform
add_executable(aoe2_presentation_transform_tests tests/cpp/presentation_transform_tests.cpp)
target_link_libraries(aoe2_presentation_transform_tests PRIVATE aoe2_interfaces)
add_test(NAME presentation_transform COMMAND aoe2_presentation_transform_tests)

# Decoupled HD asset provider tests
add_executable(aoe2_asset_tests
    tests/cpp/asset_tests.cpp
)

target_link_libraries(aoe2_asset_tests PRIVATE aoe2_interfaces)
add_test(NAME asset_tests COMMAND aoe2_asset_tests)

# Strict host-only KCET hook configuration parser tests
add_executable(aoe2_kcet_hook_config_tests tests/cpp/kcet_hook_config_tests.cpp)
target_link_libraries(aoe2_kcet_hook_config_tests PRIVATE aoe2_interfaces)
add_test(NAME kcet_hook_config_tests COMMAND aoe2_kcet_hook_config_tests)

add_executable(aoe2_native_module_switches_tests tests/cpp/native_module_switches_tests.cpp)
target_link_libraries(aoe2_native_module_switches_tests PRIVATE aoe2_interfaces)
add_test(NAME native_module_switches COMMAND aoe2_native_module_switches_tests)

add_executable(aoe2_vsync_model_tests tests/cpp/vsync_model_tests.cpp)
target_link_libraries(aoe2_vsync_model_tests PRIVATE aoe2_interfaces)
add_test(NAME vsync_model COMMAND aoe2_vsync_model_tests)

add_executable(aoe2_thread_model_tests tests/cpp/thread_model_tests.cpp)
target_link_libraries(aoe2_thread_model_tests PRIVATE aoe2_interfaces)
add_test(NAME thread_model COMMAND aoe2_thread_model_tests)

add_executable(aoe2_pad_report_tests tests/cpp/pad_report_tests.cpp)
target_link_libraries(aoe2_pad_report_tests PRIVATE aoe2_interfaces)
add_test(NAME pad_report COMMAND aoe2_pad_report_tests)
add_executable(aoe2_kt_libpad_model_tests tests/cpp/kt_libpad_model_tests.cpp)
target_link_libraries(aoe2_kt_libpad_model_tests PRIVATE aoe2_interfaces)
add_test(NAME kt_libpad_model COMMAND aoe2_kt_libpad_model_tests)

add_executable(aoe2_input_synthetic_tests tests/cpp/input_synthetic_tests.cpp)
target_include_directories(aoe2_input_synthetic_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_input_synthetic_tests PRIVATE aoe2_interfaces)
add_test(NAME input_synthetic COMMAND aoe2_input_synthetic_tests)

add_executable(aoe2_state_snapshot_tests tests/cpp/state_snapshot_tests.cpp)
target_include_directories(aoe2_state_snapshot_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_state_snapshot_tests PRIVATE aoe2_interfaces)
add_test(NAME state_snapshot COMMAND aoe2_state_snapshot_tests)

add_executable(aoe2_pad_device_tests tests/cpp/pad_device_tests.cpp)
target_link_libraries(aoe2_pad_device_tests PRIVATE aoe2_interfaces)
add_test(NAME pad_device COMMAND aoe2_pad_device_tests)
add_executable(aoe2_host_button_map_tests tests/cpp/host_button_map_tests.cpp)
target_link_libraries(aoe2_host_button_map_tests PRIVATE aoe2_interfaces)
add_test(NAME host_button_map COMMAND aoe2_host_button_map_tests)

# i.LINK socket model tests (in-memory network)
add_executable(aoe2_net_tests tests/cpp/net_tests.cpp)
target_link_libraries(aoe2_net_tests PRIVATE aoe2_interfaces)
add_test(NAME net_tests COMMAND aoe2_net_tests)

# LAN session discovery codec and directory over the shared network transport
add_executable(aoe2_lan_discovery_tests tests/cpp/lan_discovery_tests.cpp)
target_link_libraries(aoe2_lan_discovery_tests PRIVATE aoe2_interfaces)
add_test(NAME lan_discovery COMMAND aoe2_lan_discovery_tests)

# POSIX UDP backend for the i.LINK socket model (not available on Switch)
if(UNIX AND NOT NINTENDO_SWITCH)
    add_library(aoe2_udp_transport STATIC platform/desktop/src/udp_transport.cpp)
    target_link_libraries(aoe2_udp_transport PUBLIC aoe2_interfaces)
    add_executable(aoe2_udp_transport_tests tests/cpp/udp_transport_tests.cpp)
    target_link_libraries(aoe2_udp_transport_tests PRIVATE aoe2_udp_transport)
    add_test(NAME udp_transport_tests COMMAND aoe2_udp_transport_tests)
endif()

add_executable(aoe2_guest_access_tests tests/cpp/guest_access_tests.cpp)
target_link_libraries(aoe2_guest_access_tests PRIVATE aoe2_interfaces)
add_test(NAME guest_access COMMAND aoe2_guest_access_tests)

add_executable(aoe2_native_input_adapter_tests tests/cpp/native_input_adapter_tests.cpp)
target_include_directories(aoe2_native_input_adapter_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_native_input_adapter_tests PRIVATE aoe2_interfaces)
add_test(NAME native_input_adapters COMMAND aoe2_native_input_adapter_tests)

add_executable(aoe2_file_snapshot_tests tests/cpp/file_snapshot_tests.cpp)
target_include_directories(aoe2_file_snapshot_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_file_snapshot_tests PRIVATE aoe2_interfaces)
add_test(NAME file_snapshot COMMAND aoe2_file_snapshot_tests)

add_executable(aoe2_kt_file_model_tests tests/cpp/kt_file_model_tests.cpp)
target_link_libraries(aoe2_kt_file_model_tests PRIVATE aoe2_interfaces)
add_test(NAME kt_file_model COMMAND aoe2_kt_file_model_tests)

add_executable(aoe2_cdvd_model_tests tests/cpp/cdvd_model_tests.cpp tests/cpp/cdvd_read_tests.cpp)
target_link_libraries(aoe2_cdvd_model_tests PRIVATE aoe2_interfaces)
add_test(NAME cdvd_model COMMAND aoe2_cdvd_model_tests)
if(TARGET aoe2_ps2_runtime)
    target_sources(aoe2_cdvd_model_tests PRIVATE game_hooks/src/kcet_cdvd_module.cpp
        "${PS2X_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(aoe2_cdvd_model_tests PRIVATE aoe2_ps2_runtime)
    target_compile_definitions(aoe2_cdvd_model_tests PRIVATE AOE2_TEST_NATIVE_CDVD=1)
    add_executable(aoe2_cdvd_rpc_tests tests/cpp/cdvd_rpc_tests.cpp
        "${PS2X_DIR}/ps2xTest/src/test_function_table.cpp")
    target_link_libraries(aoe2_cdvd_rpc_tests PRIVATE aoe2_ps2_runtime)
    target_include_directories(aoe2_cdvd_rpc_tests PRIVATE tests/cpp)
    add_test(NAME cdvd_rpc COMMAND aoe2_cdvd_rpc_tests)
    set_tests_properties(cdvd_rpc PROPERTIES TIMEOUT 30)
endif()

add_executable(aoe2_native_file_adapter_tests tests/cpp/native_file_adapter_tests.cpp)
target_include_directories(aoe2_native_file_adapter_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_native_file_adapter_tests PRIVATE aoe2_interfaces)
add_test(NAME native_file_adapters COMMAND aoe2_native_file_adapter_tests)

add_executable(aoe2_file_differential_tests tests/cpp/file_differential_tests.cpp)
target_link_libraries(aoe2_file_differential_tests PRIVATE aoe2_interfaces)
add_test(NAME file_differential COMMAND aoe2_file_differential_tests)

add_executable(aoe2_mc_snapshot_tests tests/cpp/mc_snapshot_tests.cpp)
target_include_directories(aoe2_mc_snapshot_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_mc_snapshot_tests PRIVATE aoe2_interfaces)
add_test(NAME mc_snapshot COMMAND aoe2_mc_snapshot_tests)

add_executable(aoe2_kt_mc_model_tests tests/cpp/kt_mc_model_tests.cpp)
target_link_libraries(aoe2_kt_mc_model_tests PRIVATE aoe2_interfaces)
add_test(NAME kt_mc_model COMMAND aoe2_kt_mc_model_tests)

add_executable(aoe2_mc_storage_model_tests tests/cpp/mc_storage_model_tests.cpp)
target_link_libraries(aoe2_mc_storage_model_tests PRIVATE aoe2_interfaces)
add_test(NAME mc_storage_model COMMAND aoe2_mc_storage_model_tests)

add_executable(aoe2_native_mc_adapter_tests tests/cpp/native_mc_adapter_tests.cpp)
target_include_directories(aoe2_native_mc_adapter_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_native_mc_adapter_tests PRIVATE aoe2_interfaces)
add_test(NAME native_mc_adapters COMMAND aoe2_native_mc_adapter_tests)

add_executable(aoe2_mc_differential_tests tests/cpp/mc_differential_tests.cpp)
target_link_libraries(aoe2_mc_differential_tests PRIVATE aoe2_interfaces)
add_test(NAME mc_differential COMMAND aoe2_mc_differential_tests)

add_executable(aoe2_hd_snapshot_tests tests/cpp/hd_snapshot_tests.cpp)
target_include_directories(aoe2_hd_snapshot_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_hd_snapshot_tests PRIVATE aoe2_interfaces)
add_test(NAME hd_snapshot COMMAND aoe2_hd_snapshot_tests)

add_executable(aoe2_kt_hd_model_tests tests/cpp/kt_hd_model_tests.cpp)
target_link_libraries(aoe2_kt_hd_model_tests PRIVATE aoe2_interfaces)
add_test(NAME kt_hd_model COMMAND aoe2_kt_hd_model_tests)

add_executable(aoe2_hd_storage_model_tests tests/cpp/hd_storage_model_tests.cpp)
target_link_libraries(aoe2_hd_storage_model_tests PRIVATE aoe2_interfaces)
add_test(NAME hd_storage_model COMMAND aoe2_hd_storage_model_tests)

add_executable(aoe2_native_hd_adapter_tests tests/cpp/native_hd_adapter_tests.cpp)
target_include_directories(aoe2_native_hd_adapter_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_native_hd_adapter_tests PRIVATE aoe2_interfaces)
add_test(NAME native_hd_adapters COMMAND aoe2_native_hd_adapter_tests)

add_executable(aoe2_hd_differential_tests tests/cpp/hd_differential_tests.cpp)
target_link_libraries(aoe2_hd_differential_tests PRIVATE aoe2_interfaces)
add_test(NAME hd_differential COMMAND aoe2_hd_differential_tests)

add_executable(aoe2_gpu_snapshot_tests tests/cpp/gpu_snapshot_tests.cpp)
target_include_directories(aoe2_gpu_snapshot_tests PRIVATE game_hooks/src)
target_link_libraries(aoe2_gpu_snapshot_tests PRIVATE aoe2_interfaces)
add_test(NAME gpu_snapshot COMMAND aoe2_gpu_snapshot_tests)



















add_executable(aoe2_kt_gpu_model_tests tests/cpp/kt_gpu_model_tests.cpp)
target_link_libraries(aoe2_kt_gpu_model_tests PRIVATE aoe2_interfaces)
add_test(NAME kt_gpu_model COMMAND aoe2_kt_gpu_model_tests)

add_executable(aoe2_hud_layout_tests tests/cpp/hud_layout_tests.cpp)
target_link_libraries(aoe2_hud_layout_tests PRIVATE aoe2_interfaces)
add_test(NAME hud_layout COMMAND aoe2_hud_layout_tests)

add_executable(aoe2_jasc_palette_tests tests/cpp/jasc_palette_tests.cpp)
target_link_libraries(aoe2_jasc_palette_tests PRIVATE aoe2_interfaces)
add_test(NAME jasc_palette COMMAND aoe2_jasc_palette_tests)

add_executable(aoe2_slp_decoder_tests tests/cpp/slp_decoder_tests.cpp)
target_link_libraries(aoe2_slp_decoder_tests PRIVATE aoe2_interfaces)
add_test(NAME slp_decoder COMMAND aoe2_slp_decoder_tests)

add_executable(aoe2_gles_renderer_tests tests/cpp/gles_renderer_tests.cpp)
target_link_libraries(aoe2_gles_renderer_tests PRIVATE aoe2_interfaces)
add_test(NAME gles_renderer COMMAND aoe2_gles_renderer_tests)

add_executable(aoe2_asset_resolver_tests tests/cpp/asset_resolver_tests.cpp)
target_link_libraries(aoe2_asset_resolver_tests PRIVATE aoe2_interfaces)
add_test(NAME asset_resolver COMMAND aoe2_asset_resolver_tests)

add_executable(aoe2_gpu_native_draw_tests tests/cpp/gpu_native_draw_tests.cpp)
target_link_libraries(aoe2_gpu_native_draw_tests PRIVATE aoe2_interfaces)
add_test(NAME gpu_native_draw COMMAND aoe2_gpu_native_draw_tests)

option(AOE2_BUILD_GPU_DIFFERENTIAL_TESTS "Compare native GPU with owned generated originals" OFF)
if(AOE2_BUILD_GPU_DIFFERENTIAL_TESTS)
    include(cmake/Aoe2GpuTests.cmake)
endif()

option(AOE2_BUILD_HD_RUNTIME_TESTS "Compare HD runtime handlers with owned generated originals" OFF)
if(AOE2_BUILD_HD_RUNTIME_TESTS)
    include(cmake/Aoe2HdTests.cmake)
endif()
