// KCET contracts referenced from owner's decomp, origin commit 83a0f2f.
// No decomp function bodies copied. Current generated originals govern dispatch.
#include "map_view_module_private.hpp"
#include <aoe2/game_hooks/map_view_config.hpp>
#include "map_view_observation_private.hpp"
#include "ps2_runtime.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace aoe2::game_hooks::map_view {
namespace {
using namespace detail;
// Guest execution and Install/Teardown must be serialized by the caller. Capture
// owns its data; this mutex protects only publication and registry access.
std::unordered_map<PS2Runtime*, std::shared_ptr<State>> states;
std::shared_ptr<State> Find(PS2Runtime* runtime) {
    std::lock_guard lock(mutex);
    auto found = states.find(runtime);
    if (found == states.end()) throw std::runtime_error("map view wrapper without installation");
    return found->second;
}
void Prepare(uint8_t* ram, R5900Context* ctx, PS2Runtime& runtime, State& state) {
    // Only entry is initialization. Resume PCs must keep s0 and all original state.
    if (ctx->pc != entries[0]) return;
    Validate(runtime, state);
    uint32_t panel = getRegU32(ctx, 4);
    Bytes(ram, panel, 0x1c60);
    ObservePanelInit(ram, panel);
    if (Word(ram, panel+0x14) != 640 || Word(ram, panel+0x18) != 262)
        throw std::runtime_error("STEP2 enlargement blocked: live legacy parity gate is pending");
    if (!state.lease) {
        // Explicit legacy observes the live objects without preparing any storage.
        state.relocation.panel = panel;
        state.relocation.area = Word(ram, panel+0x1c5c);
        return;
    }
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
    if (!state.lease || ctx->pc != entries[slot]) return;
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
    if (state->lease) runtime.releaseMapArena(state.get(), state->lease.generation);
    state->relocation.clear();
}
void Install(PS2Runtime& runtime) {
    const char* configured = std::getenv("AOE2_MAP_VIEW");
    const auto mode = ReadMapViewConfig(configured, std::getenv("AOE2_MAP_VIEW_OVERRIDE"));
    Teardown(runtime);
    if (!configured) return; // Default legacy has no hooks or arena overhead.
    auto state = std::make_shared<State>();
    for (size_t i = 0; i < entries.size(); ++i) {
        state->originals[i] = runtime.lookupFunction(entries[i]);
        if (!state->originals[i] || state->originals[i] == wrappers[i])
            throw std::runtime_error("map view original unavailable");
    }
    if (mode == MapViewMode::Native) {
        state->lease = runtime.claimMapArena(state.get());
        if (!state->lease) throw std::runtime_error("map arena already owned or unavailable");
    }
    bool published = false;
    try {
        { std::lock_guard lock(mutex); states.emplace(&runtime, state); }
        published = true;
        for (size_t i = 0; i < entries.size(); ++i)
            if (!runtime.replaceFunction(entries[i], wrappers[i]))
                throw std::runtime_error("cannot install map view wrapper");
    } catch (...) {
        if (published) Teardown(runtime);
        else if (state->lease) runtime.releaseMapArena(state.get(), state->lease.generation);
        throw;
    }
}
std::optional<Snapshot> Capture(PS2Runtime& runtime) {
    std::lock_guard lock(mutex);
    auto found = states.find(&runtime);
    if (found == states.end() || (found->second->lease &&
        !runtime.validateMapArena(found->second.get(), found->second->lease.generation)))
        return std::nullopt;
    return found->second->snapshot;
}
}
