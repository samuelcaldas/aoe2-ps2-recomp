// KCET contracts referenced from owner's decomp, origin commit 83a0f2f.
// No decomp function bodies copied. Current generated originals govern
// dispatch.
#include "map_view_module_private.hpp"
#include "map_view_observation_private.hpp"
#include "ps2_runtime.h"
#include <algorithm>
#include <aoe2/game_hooks/map_view_config.hpp>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace aoe2::game_hooks::map_view {
namespace {
using namespace detail;
// Guest execution and Install/Teardown must be serialized by the caller.
// Capture owns its data; this mutex protects only publication and registry
// access.
std::unordered_map<PS2Runtime *, std::shared_ptr<State>> states;
const char *ModeName(MapViewMode mode) {
  switch (mode) {
  case MapViewMode::Legacy: return "legacy";
  case MapViewMode::Relocation: return "relocation";
  case MapViewMode::Native: return "native";
  }
  return "invalid";
}
std::optional<MapViewMode> ConfiguredMode() {
  const char *configured = std::getenv("AOE2_MAP_VIEW");
  const char *diagnostic_override = std::getenv("AOE2_MAP_VIEW_OVERRIDE");
  if (configured || diagnostic_override)
    return ReadMapViewConfig(configured, diagnostic_override);
#if defined(__SWITCH__)
  const auto path = PS2Runtime::getIoPaths().elfDirectory / "map_view.ini";
  std::ifstream file(path);
  if (!file.is_open()) return std::nullopt;
  return ReadMapViewConfigFile(file);
#else
  return std::nullopt;
#endif
}
std::shared_ptr<State> Find(PS2Runtime *runtime) {
  std::lock_guard lock(mutex);
  auto found = states.find(runtime);
  if (found == states.end())
    throw std::runtime_error("map view wrapper without installation");
  return found->second;
}
void ReapplyPanelBounds(uint8_t *ram, uint32_t panel, uint32_t w, uint32_t h) {
  std::memcpy(Bytes(ram, panel + 0x14, 4), &w, 4);
  std::memcpy(Bytes(ram, panel + 0x18, 4), &h, 4);
}
renderer::MapViewLayout ResolveOutputLayout() {
#if defined(__SWITCH__)
  auto [out_w, out_h] = DefaultMapOutput(true, appletGetOperationMode() == AppletOperationMode_Console);
#else
  auto [out_w, out_h] = DefaultMapOutput(false, false);
#endif
  if (const char *w_env = std::getenv("AOE2_WINDOW_WIDTH")) {
    uint32_t val = std::strtoul(w_env, nullptr, 10);
    if (val >= 640 && val <= 1920)
      out_w = val;
  }
  if (const char *h_env = std::getenv("AOE2_WINDOW_HEIGHT")) {
    uint32_t val = std::strtoul(h_env, nullptr, 10);
    if (val >= 448 && val <= 1080)
      out_h = val;
  }
  return renderer::MapViewLayout::ForOutput(out_w, out_h);
}
void InitializeArenaCanaries(uint8_t *ram, const Relocation &r) {
  std::memset(Bytes(ram, r.base, r.layout.total_bytes), 0,
              r.layout.total_bytes);
  for (uint32_t slot = 0; slot < 2; ++slot) {
    uint32_t base = r.base + slot * r.layout.buffer_stride;
    std::memset(Bytes(ram, base, 16), 0xA5, 16);
    std::memset(Bytes(ram, base + r.layout.buffer_stride - 16, 16), 0xA5, 16);
  }
}
void ValidateAreaReuse(uint8_t *ram, uint32_t area) {
  if (!area)
    return;
  Bytes(ram, area, 0xf54);
  if (Word(ram, area + 0x40))
    throw std::runtime_error("install map relocation before map area creation; "
                             "existing viewport cannot be reused");
}
void Prepare(uint8_t *ram, R5900Context *ctx, PS2Runtime &runtime,
             State &state) {
  if (ctx->pc != entries[0])
    return;
  Validate(runtime, state);
  uint32_t panel = getRegU32(ctx, 4);
  Bytes(ram, panel, 0x1c60);
  ObservePanelInit(ram, panel);
  if (!state.lease) {
    state.relocation.panel = panel;
    state.relocation.area = Word(ram, panel + 0x1c5c);
    return;
  }
  if (state.relocation.panel == panel) {
    ReapplyPanelBounds(ram, panel, state.relocation.target_w,
                       state.relocation.target_h);
    state.relocation.area = Word(ram, panel + 0x1c5c);
    return;
  }
  if (state.relocation.panel)
    throw std::runtime_error(
        "map panel changed: stop guest and reinstall map view module");
  ValidateAreaReuse(ram, Word(ram, panel + 0x1c5c));
  auto layout = state.mode == MapViewMode::Native ? ResolveOutputLayout()
                                                  : renderer::MapViewLayout{};
  const uint32_t width = state.mode == MapViewMode::Native
                             ? static_cast<uint32_t>(layout.map.width)
                             : 640;
  const uint32_t height = state.mode == MapViewMode::Native
                              ? static_cast<uint32_t>(layout.map.height)
                              : 262;
  // The guest's guarded scanline table advances by visible width, while the
  // surface descriptor and host snapshot use a 16-byte aligned pitch.
  // Until those paths are unified, padded widths would address different rows.
  if ((width & 15u) != 0)
    throw std::runtime_error(
        "map viewport width must be a multiple of 16 pixels");
  ReapplyPanelBounds(ram, panel, width, height);
  state.relocation.begin(panel, width, height, state.lease.base,
                         state.lease.capacity, state.lease.generation, layout);
  state.relocation.area = Word(ram, panel + 0x1c5c);
  InitializeArenaCanaries(ram, state.relocation);
  std::clog << "[map-prepare] mode=" << ModeName(state.mode)
            << " output=" << layout.output.width << 'x' << layout.output.height
            << " target=" << width << 'x' << height << " panel=" << panel
            << " bounds=" << Word(ram, panel + 0x14) << 'x'
            << Word(ram, panel + 0x18) << " area=" << state.relocation.area
            << " arena=" << state.lease.base << '\n';
}
void EnforceRedirectArgs(uint8_t *ram, R5900Context *ctx, size_t slot,
                         const Relocation &r) {
  if (slot == 1) {
    ctx->r[6] = _mm_set_epi64x(0, r.target_w);
    ctx->r[7] = _mm_set_epi64x(0, r.target_h);
  }
  if (slot == 2) {
    ctx->r[5] = _mm_set_epi64x(0, r.target_w);
    ctx->r[6] = _mm_set_epi64x(0, r.target_h);
  }
  ReapplyPanelBounds(ram, r.panel, r.target_w, r.target_h);
}
void LogRedirect(size_t slot, uint32_t ra, uint32_t s0, uint32_t panel) {
  std::clog << "[map-redirect] slot=" << slot << " ra=0x" << std::hex << ra
            << " s0=0x" << s0 << " panel=0x" << panel << std::dec << '\n';
}
uint32_t ComputeRedirectBase(uint8_t *ram, R5900Context *ctx, size_t slot,
                             Relocation &r, uint32_t area) {
  EnforceRedirectArgs(ram, ctx, slot, r);
  uint32_t w_arg = getRegU32(ctx, slot == 1 ? 6 : 5);
  uint32_t h_arg = getRegU32(ctx, slot == 1 ? 7 : 6);
  const uint32_t base = r.redirect(entries[slot], getRegU32(ctx, 31),
                                    getRegU32(ctx, 16), area, w_arg, h_arg, true);
  std::clog << "[map-redirect-result] slot=" << slot << " area=" << area
            << " target=" << r.target_w << 'x' << r.target_h
            << " passed=" << w_arg << 'x' << h_arg << " base=" << base
            << " panel=" << r.panel << '\n';
  return base;
}
void Redirect(uint8_t *ram, R5900Context *ctx, PS2Runtime &runtime,
              State &state, size_t slot) {
  if (!state.lease || ctx->pc != entries[slot])
    return;
  auto &r = state.relocation;
  const uint32_t expected = slot == 1 ? 0x31a488 : 0x31a4b4;
  LogRedirect(slot, getRegU32(ctx, 31), getRegU32(ctx, 16), r.panel);
  if (!r.panel || getRegU32(ctx, 31) != expected ||
      getRegU32(ctx, 16) != r.panel)
    return;
  Validate(runtime, state);
  const uint32_t area = getRegU32(ctx, 4);
  Bytes(ram, area, 0xf54);
  if (Word(ram, r.panel + 0x1c5c) != area)
    throw std::runtime_error("map area identity mismatch");
  uint32_t base = ComputeRedirectBase(ram, ctx, slot, r, area);
  if (!base)
    throw std::runtime_error("map relocation contract mismatch");
  WriteWord(ram, area + 0x08, base);
  WriteWord(ram, area + 0x68, base);
  WriteWord(ram, area + 0x20, r.layout.pitch);
  PrepareAreaLineTables(ram, r);
  const int reg = slot == 1 ? 10 : 8;
  ctx->r[reg] = _mm_insert_epi64(ctx->r[reg], static_cast<int64_t>(base), 0);
}
void CursorClamp(uint8_t *ram, R5900Context *ctx,
                 const renderer::MapViewLayout &layout) {
  if (ctx->pc != entries[5])
    return;
  const int32_t max_x = static_cast<int32_t>(layout.output.width) - 1;
  const int32_t max_y = static_cast<int32_t>(layout.output.height) - 1;
  int32_t x = std::clamp(static_cast<int32_t>(getRegU32(ctx, 4)), 0, max_x);
  int32_t y = std::clamp(static_cast<int32_t>(getRegU32(ctx, 5)), 0, max_y);
  std::memcpy(Bytes(ram, 0x00C5E840, 4), &x, 4);
  std::memcpy(Bytes(ram, 0x00C5E844, 4), &y, 4);
  ctx->r[2] = _mm_set_epi64x(0, 1);
  ctx->pc = getRegU32(ctx, 31);
}
void RetirePanel(R5900Context *ctx, State &state) {
  // The original routine returns the area's pool entry and clears panel+0x1c5c.
  // Invalidate host presentation before it can show a released panel again.
  if (ctx->pc != entries[6] || !state.relocation.panel ||
      getRegU32(ctx, 4) != state.relocation.panel)
    return;
  state.relocation.clear();
  std::lock_guard lock(mutex);
  ++state.lifecycle;
  state.snapshot.reset();
  state.composed_layout.reset();
}
template <size_t Slot>
void Wrapper(uint8_t *ram, R5900Context *ctx, PS2Runtime *runtime) {
  auto state = Find(runtime);
  if constexpr (Slot == 0)
    Prepare(ram, ctx, *runtime, *state);
  if constexpr (Slot == 1 || Slot == 2)
    Redirect(ram, ctx, *runtime, *state, Slot);
  if constexpr (Slot == 3 || Slot == 4)
    Observe(ram, ctx, *runtime, *state, Slot);
  if constexpr (Slot == 6)
    RetirePanel(ctx, *state);
  if constexpr (Slot == 5) {
    if (ctx->pc == entries[5]) {
      if (auto layout = ActiveLayout(*runtime)) {
        CursorClamp(ram, ctx, *layout);
        return;
      }
    }
  }
  if constexpr (Slot == 7 || Slot == 8)
    HandleAreaLineTableHook(ram, ctx, *state, Slot);
  if constexpr (Slot == 9) {
    if (HandleVSpanInitHook(ram, ctx, *state))
      return;
  }
  if constexpr (Slot == 10) {
    if (HandleVSpanResetHook(ram, ctx, *state))
      return;
  }
  if constexpr (Slot == 11) {
    if (HandlePanelRowTableHook(ram, ctx, *state))
      return;
  }
  if constexpr (Slot == 12 || Slot == 13)
    HandleDrawListAddYHook(ctx, *state, Slot);
  if constexpr (Slot == 15) {
    const uint32_t panel = getRegU32(ctx, 4);
    state->originals[Slot](ram, ctx, runtime);
    if (state->lease && panel && panel == state->relocation.panel) {
      RecordMapPanelSubmission(ram, *state);
    }
    return;
  }
  state->originals[Slot](ram, ctx, runtime);
  // Return may mean yield or tail dispatch. No completion cleanup or register
  // restoration.
}
constexpr std::array<PS2Runtime::RecompiledFunction, entries.size()> wrappers{
    Wrapper<0>,  Wrapper<1>,  Wrapper<2>,  Wrapper<3>,  Wrapper<4>,
    Wrapper<5>,  Wrapper<6>,  Wrapper<7>,  Wrapper<8>,  Wrapper<9>,
    Wrapper<10>, Wrapper<11>, Wrapper<12>, Wrapper<13>, Wrapper<14>,
    Wrapper<15>};
} // namespace
void Teardown(PS2Runtime &runtime) {
  std::shared_ptr<State> state;
  {
    std::lock_guard lock(mutex);
    auto found = states.find(&runtime);
    if (found == states.end())
      return;
    state = found->second;
    states.erase(found);
  }
  for (size_t i = 0; i < entries.size(); ++i)
    if (runtime.lookupFunction(entries[i]) == wrappers[i])
      runtime.replaceFunction(entries[i], state->originals[i]);
  if (state->lease)
    runtime.releaseMapArena(state.get(), state->lease.generation);
  state->relocation.clear();
}
void Install(PS2Runtime &runtime) {
  const auto mode = ConfiguredMode();
  Teardown(runtime);
  if (!mode) {
    std::clog << "[map-view-install] mode=disabled (legacy default)\n";
    return; // Default legacy has no hooks or arena overhead.
  }
  std::clog << "[map-view-install] mode=" << ModeName(*mode) << '\n';
  auto state = std::make_shared<State>();
  state->mode = *mode;
  for (size_t i = 0; i < entries.size(); ++i) {
    state->originals[i] = runtime.lookupFunction(entries[i]);
    if (!state->originals[i] || state->originals[i] == wrappers[i])
      throw std::runtime_error("map view original unavailable");
  }
  if (*mode == MapViewMode::Native || *mode == MapViewMode::Relocation) {
    state->lease = runtime.claimMapArena(state.get());
    if (!state->lease)
      throw std::runtime_error("map arena already owned or unavailable");
  }
  bool published = false;
  try {
    {
      std::lock_guard lock(mutex);
      states.emplace(&runtime, state);
    }
    published = true;
    for (size_t i = 0; i < entries.size(); ++i)
      if (!runtime.replaceFunction(entries[i], wrappers[i]))
        throw std::runtime_error("cannot install map view wrapper");
  } catch (...) {
    if (published)
      Teardown(runtime);
    else if (state->lease)
      runtime.releaseMapArena(state.get(), state->lease.generation);
    throw;
  }
}
std::optional<Snapshot> Capture(PS2Runtime &runtime) {
  std::lock_guard lock(mutex);
  auto found = states.find(&runtime);
  if (found == states.end() ||
      (found->second->lease &&
       !runtime.validateMapArena(found->second.get(),
                                 found->second->lease.generation)))
    return std::nullopt;
  return found->second->snapshot;
}
std::optional<Snapshot> CaptureNative(PS2Runtime &runtime) {
  std::lock_guard lock(mutex);
  auto found = states.find(&runtime);
  if (found == states.end() || found->second->mode != MapViewMode::Native ||
      !found->second->lease ||
      !runtime.validateMapArena(found->second.get(),
                                found->second->lease.generation))
    return std::nullopt;
  return found->second->snapshot;
}
bool PublishComposedLayout(PS2Runtime &runtime, const Snapshot *snapshot) {
  std::lock_guard lock(mutex);
  auto found = states.find(&runtime);
  if (found == states.end())
    return false;
  auto &state = *found->second;
  if (!snapshot) {
    state.composed_layout.reset();
    return false;
  }
  if (state.mode != MapViewMode::Native || !state.lease ||
      !runtime.validateMapArena(&state, state.lease.generation) ||
      !state.snapshot || state.snapshot->generation != snapshot->generation ||
      state.snapshot->panel != snapshot->panel ||
      state.snapshot->lifecycle != snapshot->lifecycle)
    return false;
  state.composed_layout = snapshot->layout;
  return true;
}
std::optional<renderer::MapViewLayout> ActiveLayout(PS2Runtime &runtime) {
  std::lock_guard lock(mutex);
  auto found = states.find(&runtime);
  if (found == states.end() || !found->second->lease ||
      !runtime.validateMapArena(found->second.get(),
                                found->second->lease.generation))
    return std::nullopt;
  return found->second->composed_layout;
}
std::optional<renderer::MapViewLayout> ActiveLayout() {
  std::lock_guard lock(mutex);
  for (const auto &[rt, state] : states) {
    if (state->lease &&
        rt->validateMapArena(state.get(), state->lease.generation) &&
        state->composed_layout)
      return state->composed_layout;
  }
  return std::nullopt;
}
} // namespace aoe2::game_hooks::map_view

namespace aoe2::game_hooks {

std::optional<renderer::MapViewLayout>
ActiveMapViewLayout(PS2Runtime &runtime) {
  return map_view::ActiveLayout(runtime);
}

std::optional<renderer::MapViewLayout> ActiveMapViewLayout() {
  return map_view::ActiveLayout();
}

} // namespace aoe2::game_hooks
