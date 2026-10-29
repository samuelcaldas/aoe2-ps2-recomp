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
void LogObserveCall(uint8_t* ram, size_t slot, uint32_t area, uint32_t a0, uint32_t a1) {
    std::clog << "[map-obs-call] slot=" << slot << " area=" << area
              << " a0=0x" << std::hex << a0 << " a1=0x" << a1;
    if (area && a0 && a1) {
        std::clog << " a0_w=" << std::dec << Word(ram, a0+0x38)
                  << " a0_h=" << Word(ram, a0+0x3c)
                  << " a0_src=0x" << std::hex << Word(ram, a0+0x54)
                  << " a0_98=0x" << Word(ram, a0+0x98)
                  << " a1_w=" << std::dec << Word(ram, a1+0x38)
                  << " a1_h=" << Word(ram, a1+0x3c)
                  << " a1_src=0x" << std::hex << Word(ram, a1+0x54)
                  << " a1_98=0x" << Word(ram, a1+0x98);
    }
    std::clog << std::dec << '\n';
}
void ObservePalette(uint8_t* ram, R5900Context* ctx, State& state, const Relocation& r) {
    if (getRegU32(ctx, 4) != r.area) return;
    ++state.palettes;
    const uint32_t palette_slot = Word(ram, r.area);
    Bytes(ram, palette_slot, 0x45c);
    const uint32_t source = palette_slot+0x5c;
    std::clog << "[map-palette] call=" << state.palettes << " area=" << r.area
              << " slot=" << palette_slot << " source=" << source
              << " destination=" << getRegU32(ctx, 5)
              << " hash=" << Hash(Bytes(ram, source, 1024), 1024) << '\n';
}
void LogViewportCheck(uint32_t vp, uint32_t exp, uint32_t w, uint32_t h, uint32_t src) {
    std::clog << "[map-obs-vp] vp=0x" << std::hex << vp << " exp=0x" << exp
              << " w=" << std::dec << w << " h=" << h << std::hex
              << " src=0x" << src << std::dec << '\n';
}
bool ValidateViewport(uint8_t* ram, R5900Context* ctx, const Relocation& r) {
    const uint32_t vp = getRegU32(ctx, 4);
    const uint32_t a1 = getRegU32(ctx, 5);
    if (!vp || !a1) return false;
    Bytes(ram, vp, 0x9c);
    Bytes(ram, a1, 0x9c);
    if (!((Word(ram, vp+0x98) & 0x800) && !(Word(ram, a1+0x98) & 0x200))) return false;
    const uint32_t exp = Word(ram, r.area+0x40);
    LogViewportCheck(vp, exp, Word(ram, vp+0x38), Word(ram, vp+0x3c), Word(ram, vp+0x54));
    if (r.base)
        return Word(ram, vp+0x54) == r.base + r.layout.visible_offset &&
               Word(ram, vp+0x38) == r.target_w && Word(ram, vp+0x3c) == r.target_h;
    return vp == exp;
}
void PublishSnapshot(uint8_t* ram, State& state, const Relocation& r, Snapshot snap, uint32_t w, uint32_t h) {
    const uint32_t pitch = r.layout.pitch ? r.layout.pitch : ((w + 15u) & ~15u);
    const uint32_t upload_rows = (h + 3u) & ~3u;
    const uint32_t upload_bytes = pitch * upload_rows;
    auto* indices = Bytes(ram, snap.source, upload_bytes);
    snap.indices.assign(indices, indices+upload_bytes);
    std::memcpy(snap.palette.data(), Bytes(ram, snap.palette_source, 1024), 1024);
    snap.width = w; snap.height = h; snap.pitch = pitch; snap.upload_rows = upload_rows;
    snap.layout = state.lease ? r.active_layout : renderer::MapViewLayout::ForOutput(w, h == 262 ? 448 : h);
    snap.indices_hash = Hash(snap.indices.data(), snap.indices.size());
    snap.palette_hash = Hash(snap.palette.data(), snap.palette.size());
    std::clog << "[map-submit] call=" << snap.submission << " gen=" << snap.generation
              << " area=" << snap.area << " vp=" << snap.viewport << " src=" << snap.source
              << " indices=" << snap.indices_hash << " pal=" << snap.palette_hash << '\n';
    std::lock_guard lock(mutex);
    state.snapshot = std::move(snap);
}
void RecordSubmission(uint8_t* ram, State& state, const Relocation& r, uint32_t viewport) {
    if (state.lease) CheckCanaries(ram, state);
    Snapshot snap;
    snap.generation = r.generation; snap.submission = ++state.submissions;
    snap.palette_calls = state.palettes;
    snap.panel = r.panel; snap.area = r.area; snap.viewport = viewport;
    snap.source = Word(ram, viewport+0x54);
    snap.palette_slot = Word(ram, r.area);
    Bytes(ram, snap.palette_slot, 0x45c);
    snap.palette_source = snap.palette_slot+0x5c;
    const uint32_t target_w = state.lease ? r.target_w : Word(ram, viewport+0x38);
    const uint32_t target_h = state.lease ? r.target_h : Word(ram, viewport+0x3c);
    if (Word(ram, viewport+0x38) != target_w || Word(ram, viewport+0x3c) != target_h ||
        (state.lease && snap.source != r.base+r.layout.visible_offset))
        throw std::runtime_error("map submission differs from relocation contract");
    PublishSnapshot(ram, state, r, std::move(snap), target_w, target_h);
}
void Observe(uint8_t* ram, R5900Context* ctx, PS2Runtime& runtime, State& state, size_t slot) {
    auto& r = state.relocation;
    if (ctx->pc != entries[slot]) return;
    if (r.panel) r.area = Word(ram, r.panel+0x1c5c);
    LogObserveCall(ram, slot, r.area, getRegU32(ctx, 4), getRegU32(ctx, 5));
    if (!r.area) return;
    Validate(runtime, state);
    if (slot == 4) {
        ObservePalette(ram, ctx, state, r);
        return;
    }
    if (!ValidateViewport(ram, ctx, r)) return;
    RecordSubmission(ram, state, r, getRegU32(ctx, 4));
}
}
