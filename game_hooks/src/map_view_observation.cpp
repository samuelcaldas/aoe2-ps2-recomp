// Observation owns snapshot bytes; guest execution must be stopped for teardown.
#include "map_view_observation_private.hpp"
#include <cstring>
#include <iostream>

namespace aoe2::game_hooks::map_view::detail {
std::mutex mutex;
uint8_t* Bytes(uint8_t* ram, uint32_t address, uint32_t size) {
    // These contracts refer to physical EE RAM, never MMIO or scratchpad.
    if (!ram || !address || uint64_t(address) + size > 0x4000000)
        throw std::runtime_error("map view guest range outside EE RAM");
    return ram + address;
}
uint32_t Word(uint8_t* ram, uint32_t address) {
    uint32_t value;
    std::memcpy(&value, Bytes(ram, address, 4), 4);
    return value;
}
uint64_t Hash(const uint8_t* bytes, size_t size) {
    uint64_t value = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * 1099511628211ull;
    return value;
}
void Validate(PS2Runtime& runtime, State& state) {
    if (state.lease && !runtime.validateMapArena(&state, state.lease.generation)) {
        state.relocation.clear();
        { std::lock_guard lock(mutex); state.snapshot.reset(); }
        throw std::runtime_error("map view arena invalidated; reinstall at a guest safe point");
    }
}
void CheckCanaries(uint8_t* ram, const State& state) {
    const auto& r = state.relocation;
    for (uint32_t slot = 0; slot < 2; ++slot) {
        uint32_t base = r.base + slot*r.layout.buffer_stride;
        for (uint32_t i = 0; i < 16; ++i)
            if (*Bytes(ram, base+i, 1) != 0xA5 ||
                *Bytes(ram, base+r.layout.buffer_stride-16+i, 1) != 0xA5)
                throw std::runtime_error("map view arena canary damaged");
    }
}
void ObservePanelInit(uint8_t* ram, uint32_t panel) {
    // Generated 1861D0 loads the vtable from object+0 before Method50.
    // Observe the actual map instance; input-dispatch object fields have not
    // been verified for this panel, so no parent/root interpretation is made.
    const uint32_t table = Word(ram, panel);
    std::clog << "[map-panel-init] panel=" << panel << " vtable=" << table;
    if (table && uint64_t(table) + 0xf4 <= 0x4000000) {
        std::clog << " slot_50=" << Word(ram, table+0x50)
                  << " slot_b0=" << Word(ram, table+0xb0)
                  << " slot_b4=" << Word(ram, table+0xb4)
                  << " slot_f0=" << Word(ram, table+0xf0);
    } else {
        // Diagnostics must not dereference an invalid guest vtable.
        std::clog << " slots=unreadable";
    }
    std::clog << '\n';
}
void Observe(uint8_t* ram, R5900Context* ctx, PS2Runtime& runtime, State& state, size_t slot) {
    auto& r = state.relocation;
    if (ctx->pc != entries[slot]) return;
    // Panel initialization precedes original area allocation. Discover its live
    // result at observation time; observer mode never enters relocation.begin().
    if (!state.lease && r.panel) r.area = Word(ram, r.panel+0x1c5c);
    if (!r.area) return;
    Validate(runtime, state);
    if (slot == 4) {
        if (getRegU32(ctx, 4) == r.area) {
            ++state.palettes;
            const uint32_t palette_slot = Word(ram, r.area);
            Bytes(ram, palette_slot, 0x45c);
            const uint32_t source = palette_slot+0x5c;
            // Observe source, not destination: 1AE350 tail-dispatches its copy loop.
            std::clog << "[map-palette] call=" << state.palettes << " area=" << r.area
                      << " slot=" << palette_slot << " source=" << source
                      << " destination=" << getRegU32(ctx, 5)
                      << " hash=" << Hash(Bytes(ram, source, 1024), 1024) << '\n';
        }
        return;
    }
    const uint32_t viewport = getRegU32(ctx, 4);
    if (viewport != Word(ram, r.area+0x40)) return;
    Bytes(ram, viewport, 0x9c);
    Bytes(ram, getRegU32(ctx, 5), 0x9c);
    if (!(Word(ram, viewport+0x98)&0x800) || (Word(ram, getRegU32(ctx, 5)+0x98)&0x200)) return;
    if (state.lease) CheckCanaries(ram, state);
    Snapshot snap;
    snap.generation = r.generation; snap.submission = ++state.submissions;
    snap.palette_calls = state.palettes;
    snap.panel = r.panel; snap.area = r.area; snap.viewport = viewport;
    snap.source = Word(ram, viewport+0x54);
    snap.palette_slot = Word(ram, r.area);
    Bytes(ram, snap.palette_slot, 0x45c);
    snap.palette_source = snap.palette_slot+0x5c;
    // Keep both the source address and call number: two uploads need not be two buffers.
    // Submission ABI: +0x38 is the row width, +0x3c is rounded to four rows.
    if (Word(ram, viewport+0x38) != 640 || Word(ram, viewport+0x3c) != 262 ||
        (state.lease && snap.source != r.base+r.layout.visible_offset))
        throw std::runtime_error("map submission differs from legacy relocation contract");
    auto* indices = Bytes(ram, snap.source, 640*264);
    snap.indices.assign(indices, indices+640*264);
    std::memcpy(snap.palette.data(), Bytes(ram, snap.palette_source, 1024), 1024);
    snap.indices_hash = Hash(snap.indices.data(), snap.indices.size());
    snap.palette_hash = Hash(snap.palette.data(), snap.palette.size());
    std::clog << "[map-submit] call=" << snap.submission << " generation=" << snap.generation
              << " area=" << snap.area << " viewport=" << snap.viewport << " source=" << snap.source
              << " indices=" << snap.indices_hash << " palette_slot=" << snap.palette_slot
              << " palette_source=" << snap.palette_source << " palette=" << snap.palette_hash
              << " palette_calls=" << snap.palette_calls << '\n';
    std::lock_guard lock(mutex);
    state.snapshot = std::move(snap);
}
}
