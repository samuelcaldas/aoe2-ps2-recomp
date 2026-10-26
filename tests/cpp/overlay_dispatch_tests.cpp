// Overlay dispatch: several overlays share one guest region; the id stored in guest
// memory at the region's id address selects which overlay's functions are live.
#include "ps2_runtime.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

#if AOE2_HAS_DECOMPILED_OVERLAYS
int RegisterGeneratedOverlays(PS2Runtime& runtime);
void Hook_OverlayLoad(uint8_t* rdram, R5900Context* context, PS2Runtime* runtime);
#endif

namespace {
constexpr uint32_t kBase = 0x1D98000;
constexpr uint32_t kSize = 0x100000;
constexpr uint32_t kIdAddress = kBase + 4;
constexpr uint32_t kFunction = kBase + 0x100;
int g_called = 0;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void overlay_four(uint8_t*, R5900Context*, PS2Runtime*) { g_called = 4; }
void overlay_six(uint8_t*, R5900Context*, PS2Runtime*) { g_called = 6; }
void loader_stub(uint8_t*, R5900Context*, PS2Runtime*) {}

void load_overlay(PS2Runtime& runtime, uint32_t id) {
    std::memcpy(runtime.memory().getRDRAM() + kIdAddress, &id, sizeof(id));
}

void request_overlay_load(PS2Runtime& runtime, const char* filename, uint32_t address) {
    auto* rdram = runtime.memory().getRDRAM();
    constexpr uint32_t filename_address = 0x1000;
    std::strncpy(reinterpret_cast<char*>(rdram + filename_address), filename, 64);

    R5900Context context{};
    context.pc = 0x002240B0u;
    context.r[4] = _mm_set_epi64x(0, filename_address);
    context.r[5] = _mm_set_epi64x(0, address);
    context.r[31] = _mm_set_epi64x(0, 0x12345678u);
    runtime.lookupFunction(context.pc)(rdram, &context, &runtime);
    require(getRegU32(&context, 2) == 1u, "overlay loader did not report success");
    require(context.pc == 0x12345678u, "overlay loader did not return to caller");
}

void call(PS2Runtime& runtime, uint32_t address) {
    R5900Context context{};
    runtime.lookupFunction(address)(runtime.memory().getRDRAM(), &context, &runtime);
}

void selects_the_loaded_overlays_function() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    require(runtime.addOverlayRegion(kBase, kSize, kIdAddress), "region rejected");
    require(runtime.registerOverlayFunction(4, kFunction, overlay_four), "overlay 4 registration failed");
    require(runtime.registerOverlayFunction(6, kFunction, overlay_six), "overlay 6 registration failed");

    load_overlay(runtime, 4);
    require(runtime.hasFunction(kFunction), "overlay 4 function not visible");
    call(runtime, kFunction);
    require(g_called == 4, "overlay 4 dispatched wrongly");
    load_overlay(runtime, 6);
    call(runtime, kFunction);
    require(g_called == 6, "overlay 6 dispatched wrongly");
}

void unknown_overlay_or_address_is_missing_not_guessed() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    require(runtime.addOverlayRegion(kBase, kSize, kIdAddress), "region rejected");
    require(runtime.registerOverlayFunction(4, kFunction, overlay_four), "registration failed");
    load_overlay(runtime, 9);
    require(!runtime.hasFunction(kFunction), "unloaded overlay id resolved a function");
    load_overlay(runtime, 4);
    require(!runtime.hasFunction(kFunction + 4), "unregistered address resolved");
    require(!runtime.hasFunction(kFunction + 2), "unaligned address resolved");
}

void rejects_invalid_registrations() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    require(!runtime.registerOverlayFunction(4, kFunction, overlay_four), "registered without a region");
    require(!runtime.addOverlayRegion(kBase + 2, kSize, kIdAddress), "unaligned region accepted");
    require(!runtime.addOverlayRegion(kBase, 0, kIdAddress), "empty region accepted");
    require(!runtime.addOverlayRegion(kBase, kSize, kBase + kSize), "id address outside the region accepted");
    require(runtime.addOverlayRegion(kBase, kSize, kIdAddress), "valid region rejected");
    require(!runtime.addOverlayRegion(kBase + 0x1000, kSize, kBase + 0x1000), "overlapping region accepted");
    require(!runtime.registerOverlayFunction(4, kBase - 4, overlay_four), "address outside the region accepted");
    require(runtime.registerOverlayFunction(4, kFunction, overlay_four), "valid registration rejected");
    require(!runtime.registerOverlayFunction(4, kFunction, overlay_six), "duplicate registration accepted");
}

#if AOE2_HAS_DECOMPILED_OVERLAYS
void tests_decompiled_overlay_registration_and_dispatch() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    int registered = RegisterGeneratedOverlays(runtime);
    require(registered == 8, "expected 8 overlays registered");

    struct Entry {
        uint32_t overlay_id;
        uint32_t id_addr;
        uint32_t call_addr;
    };

    const Entry entries[] = {
        {2, 0x01ED7004, 0x01ED7080}, // MO_OV
        {3, 0x01D98004, 0x01D98080}, // RMMMEM
        {4, 0x01D98004, 0x01D98080}, // LOBBY
        {5, 0x01D98004, 0x01D98080}, // DCOMP
        {6, 0x01D98004, 0x01D98080}, // SMENU
        {7, 0x01D98004, 0x01D98080}, // MULTIM
        {8, 0x01F68004, 0x01F68080}, // THELP
        {9, 0x01D98004, 0x01D98080}, // HIST
    };

    for (const auto& entry : entries) {
        std::memcpy(runtime.memory().getRDRAM() + (entry.id_addr & 0x01FFFFFFu), &entry.overlay_id, sizeof(entry.overlay_id));
        require(runtime.hasFunction(entry.call_addr), "entry point function not found for overlay");
        R5900Context ctx{};
        auto fn = runtime.lookupFunction(entry.call_addr);
        require(fn != nullptr, "lookupFunction returned null for overlay");
        fn(runtime.memory().getRDRAM(), &ctx, &runtime);
        require(ctx.pc == getRegU32(&ctx, 31), "pc was not updated to return address");
    }
}
void unknown_loader_requests_fail_without_claiming_success() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    runtime.registerFunction(0x002240B0u, loader_stub);
    require(RegisterGeneratedOverlays(runtime) == 8, "overlay registration failed");
    auto* rdram = runtime.memory().getRDRAM();
    std::memset(rdram + 0x1000, 'X', 64);
    R5900Context context{};
    context.pc = 0x002240B0u;
    context.r[4] = _mm_set_epi64x(0, 0x1000);
    context.r[5] = _mm_set_epi64x(0, kBase);
    context.r[31] = _mm_set_epi64x(0, 0x12345678u);
    runtime.lookupFunction(context.pc)(rdram, &context, &runtime);
    require(getRegU32(&context, 2) == 0u, "unknown overlay request reported success");
    require(context.pc == 0x12345678u, "failed overlay request did not return to caller");
}

void loader_transitions_dispatch_the_active_entry() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    require(runtime.addOverlayRegion(kBase, kSize, kIdAddress), "region rejected");
    require(runtime.registerOverlayFunction(4, kBase + 0x80, overlay_four), "lobby registration failed");
    require(runtime.registerOverlayFunction(6, kBase + 0x80, overlay_six), "smenu registration failed");
    runtime.registerFunction(0x002240B0u, Hook_OverlayLoad);
    for (const auto id : {6, 4, 6}) {
        request_overlay_load(runtime, id == 6 ? "smenu_cd.bin" : "lobby_cd.bin", kBase);
        R5900Context context{};
        context.pc = kBase + 0x80;
        g_called = 0;
        runtime.dispatchGuestBranch(runtime.memory().getRDRAM(), &context, kBase + 0x80,
            0x1000, 0x1008, PS2Runtime::GuestBranchKind::IndirectCall, "JALR");
        require(g_called == id, "loader transition dispatched the wrong overlay entry");
    }
}

void loader_names_select_overlay_ids_and_dispatch_entries() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    runtime.registerFunction(0x002240B0u, loader_stub);
    require(RegisterGeneratedOverlays(runtime) == 8, "overlay registration failed");

    struct LoadCase { const char* filename; uint32_t id; };
    const LoadCase cases[] = {
        {"smenu_cd.bin", 6}, {"SMENU_CD.BIN", 6}, {"cdrom0:\\SMENU_CD.BIN;1", 6},
        {"lobby_cd.bin", 4}, {"LoBbY_Cd.BiN", 4}, {"cdrom0:\\LOBBY_CD.BIN;1", 4},
    };
    auto* rdram = runtime.memory().getRDRAM();
    for (const auto& test : cases) {
        request_overlay_load(runtime, test.filename, kBase);
        uint32_t magic = 0;
        uint32_t id = 0;
        uint32_t load_address = 0;
        std::memcpy(&magic, rdram + kBase, sizeof(magic));
        std::memcpy(&id, rdram + kIdAddress, sizeof(id));
        std::memcpy(&load_address, rdram + kBase + 8, sizeof(load_address));
        require(magic == 0x336F574Du, "overlay header magic missing");
        require(id == test.id, "loader stored incorrect overlay id");
        require(load_address == kBase, "loader stored incorrect load address");
        require(runtime.hasFunction(kBase + 0x80), "loaded overlay entry was not visible");
        require(runtime.lookupFunction(kBase + 0x80) != nullptr,
                "loaded overlay entry did not resolve for dispatch");
    }
}
#endif
}

int main() {
    try {
        selects_the_loaded_overlays_function();
        unknown_overlay_or_address_is_missing_not_guessed();
        rejects_invalid_registrations();
#if AOE2_HAS_DECOMPILED_OVERLAYS
        tests_decompiled_overlay_registration_and_dispatch();
        loader_names_select_overlay_ids_and_dispatch_entries();
        loader_transitions_dispatch_the_active_entry();
        unknown_loader_requests_fail_without_claiming_success();
#endif
        std::cout << "overlay dispatch tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
