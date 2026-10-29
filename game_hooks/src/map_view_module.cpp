// KCET contracts referenced from owner's decomp, origin commit 83a0f2f.
// No decomp function bodies copied. Current generated originals govern dispatch.
#include "map_view_module_private.hpp"
#include <aoe2/game_hooks/map_view_config.hpp>
#include "game_overrides.h"
#include "ps2_runtime.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace aoe2::game_hooks::map_view {
namespace {
constexpr std::array<uint32_t, 5> entries{0x31a410, 0x1ab7e0, 0x1abac0, 0x21acd0, 0x1ae350};
struct State {
    Relocation relocation;
    MapArenaLease lease;
    std::array<PS2Runtime::RecompiledFunction, 5> originals{};
    uint64_t submissions = 0, palettes = 0;
    std::optional<Snapshot> snapshot;
};
// Guest execution and Install/Teardown must be serialized by the caller. Capture
// owns its data; this mutex protects only publication and registry access.
std::mutex mutex;
std::unordered_map<PS2Runtime*, std::shared_ptr<State>> states;
std::shared_ptr<State> Find(PS2Runtime* runtime) {
    std::lock_guard lock(mutex);
    auto found = states.find(runtime);
    if (found == states.end()) throw std::runtime_error("map view wrapper without installation");
    return found->second;
}
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
    if (!runtime.validateMapArena(&state, state.lease.generation)) {
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
void Prepare(uint8_t* ram, R5900Context* ctx, PS2Runtime& runtime, State& state) {
    // Only entry is initialization. Resume PCs must keep s0 and all original state.
    if (ctx->pc != entries[0]) return;
    Validate(runtime, state);
    uint32_t panel = getRegU32(ctx, 4);
    Bytes(ram, panel, 0x1c60);
    if (Word(ram, panel+0x14) != 640 || Word(ram, panel+0x18) != 262)
        throw std::runtime_error("STEP2 enlargement blocked: live legacy parity gate is pending");
    if (state.relocation.panel == panel) return;
    if (state.relocation.panel)
        throw std::runtime_error("map panel changed: stop guest and reinstall map view module");
    const uint32_t existing_area = Word(ram, panel+0x1c5c);
    // 1ABAC0 skips allocation when dimensions and viewport already match. Merely
    // changing its buffer argument cannot relocate an already initialized area.
    // Require installation before area creation rather than mutate shared internals.
    if (existing_area) Bytes(ram, existing_area, 0xf54);
    if (existing_area && Word(ram, existing_area+0x40))
        throw std::runtime_error("install map relocation before map area creation; existing viewport cannot be reused");
    auto& r = state.relocation;
    r.begin(panel, 640, 262, state.lease.base, state.lease.capacity, state.lease.generation);
    // Existing area means this invocation may only take the reconfiguration path.
    r.area = Word(ram, panel+0x1c5c);
    std::memset(Bytes(ram, r.base, r.layout.total_bytes), 0, r.layout.total_bytes);
    for (uint32_t slot = 0; slot < 2; ++slot) {
        uint32_t base = r.base + slot*r.layout.buffer_stride;
        std::memset(Bytes(ram, base, 16), 0xA5, 16);
        std::memset(Bytes(ram, base+r.layout.buffer_stride-16, 16), 0xA5, 16);
    }
}
void Redirect(uint8_t* ram, R5900Context* ctx, PS2Runtime& runtime, State& state, size_t slot) {
    // Never rewrite arguments on partial/resumed calls into shared routines.
    if (ctx->pc != entries[slot]) return;
    auto& r = state.relocation;
    const uint32_t expected = slot == 1 ? 0x31a488 : 0x31a4b4;
    if (!r.panel || getRegU32(ctx, 31) != expected || getRegU32(ctx, 16) != r.panel) return;
    Validate(runtime, state);
    const uint32_t area = getRegU32(ctx, 4);
    Bytes(ram, area, 0xf54);
    if (Word(ram, r.panel+0x1c5c) != area)
        throw std::runtime_error("map area identity mismatch");
    uint32_t base = r.redirect(entries[slot], getRegU32(ctx, 31), getRegU32(ctx, 16), area,
                              getRegU32(ctx, slot == 1 ? 6 : 5),
                              getRegU32(ctx, slot == 1 ? 7 : 6), true);
    if (!base) throw std::runtime_error("map relocation contract mismatch");
    // Both original routines receive the storage base before their first write.
    const int reg = slot == 1 ? 10 : 8;
    ctx->r[reg] = _mm_insert_epi64(ctx->r[reg], static_cast<int64_t>(base), 0);
}
void Observe(uint8_t* ram, R5900Context* ctx, PS2Runtime& runtime, State& state, size_t slot) {
    auto& r = state.relocation;
    if (!r.area || ctx->pc != entries[slot]) return;
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
    CheckCanaries(ram, state);
    Snapshot snap;
    snap.generation = r.generation; snap.submission = ++state.submissions;
    snap.palette_calls = state.palettes;
    snap.panel = r.panel; snap.area = r.area; snap.viewport = viewport;
    snap.source = Word(ram, viewport+0x54);
    snap.palette_slot = Word(ram, r.area);
    Bytes(ram, snap.palette_slot, 0x45c);
    snap.palette_source = snap.palette_slot+0x5c;
    // Keep both the source address and call number: two uploads need not be two buffers.
    if (Word(ram, viewport+0x3c) != 640 || Word(ram, viewport+0x38) != 262 ||
        snap.source != r.base+r.layout.visible_offset)
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
template<size_t Slot>
void Wrapper(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    auto state = Find(runtime);
    if constexpr (Slot == 0) Prepare(ram, ctx, *runtime, *state);
    if constexpr (Slot == 1 || Slot == 2) Redirect(ram, ctx, *runtime, *state, Slot);
    if constexpr (Slot == 3 || Slot == 4) Observe(ram, ctx, *runtime, *state, Slot);
    state->originals[Slot](ram, ctx, runtime);
    // Return may mean yield or tail dispatch. No completion cleanup or register restoration.
}
constexpr std::array<PS2Runtime::RecompiledFunction, 5> wrappers{
    Wrapper<0>, Wrapper<1>, Wrapper<2>, Wrapper<3>, Wrapper<4>};
}
void Teardown(PS2Runtime& runtime) {
    std::shared_ptr<State> state;
    { std::lock_guard lock(mutex);
      auto found = states.find(&runtime);
      if (found == states.end()) return;
      state = found->second;
      states.erase(found); }
    for (size_t i = 0; i < entries.size(); ++i)
        if (runtime.lookupFunction(entries[i]) == wrappers[i])
            runtime.replaceFunction(entries[i], state->originals[i]);
    runtime.releaseMapArena(state.get(), state->lease.generation);
    state->relocation.clear();
}
void Install(PS2Runtime& runtime) {
    const auto mode = ReadMapViewConfig(std::getenv("AOE2_MAP_VIEW"), std::getenv("AOE2_MAP_VIEW_OVERRIDE"));
    Teardown(runtime);
    if (mode != MapViewMode::Native) return;
    auto state = std::make_shared<State>();
    for (size_t i = 0; i < entries.size(); ++i) {
        state->originals[i] = runtime.lookupFunction(entries[i]);
        if (!state->originals[i] || state->originals[i] == wrappers[i])
            throw std::runtime_error("map view original unavailable");
    }
    state->lease = runtime.claimMapArena(state.get());
    if (!state->lease) throw std::runtime_error("map arena already owned or unavailable");
    { std::lock_guard lock(mutex); states.emplace(&runtime, state); }
    try {
        for (size_t i = 0; i < entries.size(); ++i)
            if (!runtime.replaceFunction(entries[i], wrappers[i]))
                throw std::runtime_error("cannot install map view wrapper");
    } catch (...) { Teardown(runtime); throw; }
}
std::optional<Snapshot> Capture(PS2Runtime& runtime) {
    std::lock_guard lock(mutex);
    auto found = states.find(&runtime);
    if (found == states.end() || !runtime.validateMapArena(found->second.get(), found->second->lease.generation))
        return std::nullopt;
    return found->second->snapshot;
}
}
// Registration is intentionally performed by the integration owner after removing
// InstallMapViewOverride from shape tracing and wiring Teardown before runtime destruction.
