// Overlay dispatch: several overlays share one guest region; the id stored in guest
// memory at the region's id address selects which overlay's functions are live.
#include "ps2_runtime.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

#if AOE2_HAS_RECOMPILED_OVERLAYS
int RegisterGeneratedOverlays(PS2Runtime& runtime);
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

#if AOE2_HAS_RECOMPILED_OVERLAYS
// The real loader (not replaced here) writes the whole file, header included, to the region base; the
// header's id at base+4 selects the overlay. Main-menu id 6, lobby id 4, both entered at base+0x80.
void recompiled_registration_follows_the_loaded_header() {
    PS2Runtime runtime;
    require(runtime.memory().initialize(), "memory initialization failed");
    runtime.registerFunction(0x002240B0u, loader_stub);
    require(RegisterGeneratedOverlays(runtime) == 8, "overlay registration failed");
    require(runtime.lookupFunction(0x002240B0u) == loader_stub, "overlay registration replaced the disc loader");
    load_overlay(runtime, 0);
    require(!runtime.hasFunction(kBase + 0x80), "an unloaded region resolved an overlay entry");
    for (const uint32_t id : {6u, 4u}) {
        load_overlay(runtime, id);
        require(runtime.hasFunction(kBase + 0x80), "loaded overlay entry was not visible");
        require(runtime.lookupFunction(kBase + 0x80) != nullptr, "loaded overlay entry did not resolve");
    }
}
#endif
}

int main() {
    try {
        selects_the_loaded_overlays_function();
        unknown_overlay_or_address_is_missing_not_guessed();
        rejects_invalid_registrations();
#if AOE2_HAS_RECOMPILED_OVERLAYS
        recompiled_registration_follows_the_loaded_header();
#endif
        std::cout << "overlay dispatch tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
