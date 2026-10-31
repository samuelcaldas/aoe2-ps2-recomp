// Observation owns snapshot bytes; guest execution must be stopped for teardown.
#include "map_view_observation_private.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <string>

extern void FUN_0017dc40_0x17dc40(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_0017dca0_0x17dca0(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_001820c0_0x1820c0(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_001820a0_0x1820a0(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_0017e350_0x17e350(uint8_t*, R5900Context*, PS2Runtime*);

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
uint16_t Half(uint8_t* ram, uint32_t address) {
    uint16_t value;
    std::memcpy(&value, Bytes(ram, address, 2), 2);
    return value;
}
void WriteWord(uint8_t* ram, uint32_t address, uint32_t value) {
    std::memcpy(Bytes(ram, address, 4), &value, 4);
}
uint64_t Hash(const uint8_t* bytes, size_t size) {
    uint64_t value = 14695981039346656037ull;
    for (size_t i = 0; i < size; ++i) value = (value ^ bytes[i]) * 1099511628211ull;
    return value;
}
void Validate(PS2Runtime& runtime, State& state) {
    if (state.lease && !runtime.validateMapArena(&state, state.lease.generation)) {
        state.relocation.clear();
        { std::lock_guard lock(mutex); state.snapshot.reset(); state.composed_layout.reset(); }
        throw std::runtime_error("map view arena invalidated; reinstall at a guest safe point");
    }
}
void CheckCanaries(uint8_t* ram, const State& state) {
    const auto& r = state.relocation;
    for (uint32_t slot = 0; slot < 2; ++slot) {
        uint32_t base = r.base + slot*r.layout.buffer_stride;
        for (uint32_t i = 0; i < 16; ++i) {
            const uint8_t leading = *Bytes(ram, base+i, 1);
            const uint8_t trailing = *Bytes(ram, base+r.layout.buffer_stride-16+i, 1);
            if (leading != 0xA5 || trailing != 0xA5) {
                throw std::runtime_error("map view arena canary damaged: slot=" +
                    std::to_string(slot) + (leading != 0xA5 ? " leading" : " trailing") +
                    " byte=" + std::to_string(i) + " value=" +
                    std::to_string(leading != 0xA5 ? leading : trailing));
            }
        }
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
        // The guest viewport descriptor stores height at +0x38 and width at
        // +0x3c (see render_viewport_21ae90's clipping contract).
        std::clog << " a0_w=" << std::dec << Word(ram, a0+0x3c)
                  << " a0_h=" << Word(ram, a0+0x38)
                  << " a0_src=0x" << std::hex << Word(ram, a0+0x54)
                  << " a0_98=0x" << Word(ram, a0+0x98)
                  << " a1_w=" << std::dec << Word(ram, a1+0x3c)
                  << " a1_h=" << Word(ram, a1+0x38)
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
    std::memcpy(state.last_palette.data(), Bytes(ram, source, 1024), 1024);
    state.has_last_palette = true;
    if (state.snapshot) {
        std::memcpy(state.snapshot->palette.data(), state.last_palette.data(), 1024);
        state.snapshot->palette_hash = Hash(state.snapshot->palette.data(), 1024);
    }
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
void RedirectNativeViewport(uint8_t* ram, R5900Context* ctx,
                            const State& state, const Relocation& r) {
    if (state.mode != MapViewMode::Native || !state.lease || !r.base || !r.area) return;
    const uint32_t vp = getRegU32(ctx, 4);
    const uint32_t other = getRegU32(ctx, 5);
    if (!vp || !other) return;
    Bytes(ram, vp, 0x9c);
    Bytes(ram, other, 0x9c);
    if (!(Word(ram, vp + 0x98) & 0x800) || (Word(ram, other + 0x98) & 0x200))
        return;

    const uint32_t exp = Word(ram, r.area + 0x40);
    if (!exp) return;
    Bytes(ram, exp, 0x9c);
    const uint32_t previous_height = Word(ram, exp + 0x38);
    const uint32_t previous_width = Word(ram, exp + 0x3c);
    const uint32_t previous_pitch = Word(ram, exp + 0x40);
    const uint32_t previous_source = Word(ram, exp + 0x54);
    const uint32_t pitch = r.layout.pitch;
    const uint32_t source = r.base + r.layout.visible_offset;
    WriteWord(ram, exp + 0x38, r.target_h);
    WriteWord(ram, exp + 0x3c, r.target_w);
    WriteWord(ram, exp + 0x40, pitch);
    WriteWord(ram, exp + 0x54, source);
    if (Word(ram, exp + 0x14) == 0 && Word(ram, vp + 0x14) != 0)
        WriteWord(ram, exp + 0x14, Word(ram, vp + 0x14));
    std::clog << "[map-viewport-redirect] vp=0x" << std::hex << exp
              << " source=0x" << previous_source << "->0x" << source
              << std::dec << " size=" << previous_width << 'x' << previous_height
              << "->" << r.target_w << 'x' << r.target_h
              << " pitch=" << previous_pitch << "->" << pitch << '\n';
}
bool ValidateViewport(uint8_t* ram, R5900Context* ctx, const Relocation& r) {
    const uint32_t vp = getRegU32(ctx, 4);
    const uint32_t a1 = getRegU32(ctx, 5);
    if (!vp || !a1) return false;
    Bytes(ram, vp, 0x9c);
    Bytes(ram, a1, 0x9c);
    if (!((Word(ram, vp+0x98) & 0x800) && !(Word(ram, a1+0x98) & 0x200))) return false;
    const uint32_t exp = Word(ram, r.area+0x40);
    if (r.base) {
        if (!exp) return false;
        Bytes(ram, exp, 0x9c);
        LogViewportCheck(vp, exp, Word(ram, exp+0x3c), Word(ram, exp+0x38), Word(ram, exp+0x54));
        return Word(ram, exp+0x54) == r.base + r.layout.visible_offset &&
               Word(ram, exp+0x38) == r.target_h && Word(ram, exp+0x3c) == r.target_w;
    }
    LogViewportCheck(vp, exp, Word(ram, vp+0x3c), Word(ram, vp+0x38), Word(ram, vp+0x54));
    return vp == exp;
}
void PublishSnapshot(uint8_t* ram, State& state, const Relocation& r, Snapshot snap, uint32_t w, uint32_t h) {
    const uint32_t pitch = r.layout.pitch ? r.layout.pitch : ((w + 15u) & ~15u);
    const uint32_t upload_rows = (h + 3u) & ~3u;
    const uint32_t upload_bytes = pitch * upload_rows;
    auto* indices = Bytes(ram, snap.source, upload_bytes);
    snap.indices.assign(indices, indices+upload_bytes);
    if (snap.palette_source) {
        std::memcpy(snap.palette.data(), Bytes(ram, snap.palette_source, 1024), 1024);
    }
    bool all_zero = true;
    for (uint8_t b : snap.palette) {
        if (b != 0) { all_zero = false; break; }
    }
    if (all_zero && state.has_last_palette) {
        std::memcpy(snap.palette.data(), state.last_palette.data(), 1024);
    } else if (!all_zero) {
        std::memcpy(state.last_palette.data(), snap.palette.data(), 1024);
        state.has_last_palette = true;
    }
    snap.width = w; snap.height = h; snap.pitch = pitch; snap.upload_rows = upload_rows;
    snap.layout = state.lease ? r.active_layout : renderer::MapViewLayout::ForOutput(w, h == 262 ? 448 : h);
    snap.indices_hash = Hash(snap.indices.data(), snap.indices.size());
    snap.palette_hash = Hash(snap.palette.data(), snap.palette.size());
    size_t nonzero_indices = 0;
    std::array<uint64_t, 256> index_counts{};
    for (uint32_t row = 0; row < h; ++row) {
        const auto* pixels = snap.indices.data() + uint64_t{row} * pitch;
        for (uint32_t column = 0; column < w; ++column) {
            ++index_counts[pixels[column]];
            nonzero_indices += pixels[column] != 0;
        }
    }
    size_t used_palette = 0;
    size_t opaque_palette = 0;
    for (size_t i = 0; i < 256; ++i) {
        const auto* color = snap.palette.data() + i * 4;
        used_palette += index_counts[i] != 0;
        opaque_palette += color[3] != 0;
    }
    std::clog << "[map-submit] call=" << snap.submission << " gen=" << snap.generation
              << " area=" << snap.area << " vp=" << snap.viewport << " src=" << snap.source
              << " indices=" << snap.indices_hash << " pal=" << snap.palette_hash
              << " nonzero=" << nonzero_indices << '/' << uint64_t{w}*h
              << " used-colors=" << used_palette << " opaque-colors=" << opaque_palette
              << " index0-rgba=" << unsigned(snap.palette[0]) << ',' << unsigned(snap.palette[1])
              << ',' << unsigned(snap.palette[2]) << ',' << unsigned(snap.palette[3]) << '\n';
    for (size_t i = 0; i < index_counts.size(); ++i) {
        if (index_counts[i] == 0) continue;
        const auto* color = snap.palette.data() + i * 4;
        std::clog << "[map-index] index=" << i << " count=" << index_counts[i]
                  << " rgba=" << unsigned(color[0]) << ',' << unsigned(color[1]) << ','
                  << unsigned(color[2]) << ',' << unsigned(color[3]) << '\n';
    }
    std::lock_guard lock(mutex);
    state.snapshot = std::move(snap);
}
void RecordSubmission(uint8_t* ram, State& state, const Relocation& r, uint32_t viewport) {
    if (state.lease) CheckCanaries(ram, state);
    Snapshot snap;
    snap.generation = r.generation; snap.submission = ++state.submissions;
    snap.lifecycle = state.lifecycle;
    snap.palette_calls = state.palettes;
    snap.panel = r.panel; snap.area = r.area; snap.viewport = viewport;
    snap.source = Word(ram, viewport+0x54);
    // 21A680 builds TEX0 from viewport+0x14: its +2 halfword selects the CLUT
    // slot and +4 points at the raw RGBA palette copied by 21B440. The map
    // area's first word is not guaranteed to be the palette active for this
    // upload, so snapshot the palette from the same viewport as the pixels.
    snap.palette_slot = Word(ram, viewport + 0x14);
    if (!snap.palette_slot) {
        std::clog << "[map-submit-skip] viewport has no active CLUT slot\n";
        return;
    }
    Bytes(ram, snap.palette_slot, 8);
    snap.palette_source = Word(ram, snap.palette_slot + 4);
    Bytes(ram, snap.palette_source, 1024);
    std::clog << "[map-palette-source] viewport=" << viewport
              << " slot=0x" << std::hex << snap.palette_slot
              << " clut=0x" << (Word(ram, snap.palette_slot + 2) & 0xffffu)
              << " data=0x" << snap.palette_source << std::dec << '\n';
    const uint32_t target_w = state.lease ? r.target_w : Word(ram, viewport+0x3c);
    const uint32_t target_h = state.lease ? r.target_h : Word(ram, viewport+0x38);
    if (Word(ram, viewport+0x3c) != target_w || Word(ram, viewport+0x38) != target_h ||
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
    if (slot == 3)
        RedirectNativeViewport(ram, ctx, state, r);
    if (slot == 4) {
        ObservePalette(ram, ctx, state, r);
        return;
    }
    if (!ValidateViewport(ram, ctx, r)) return;
    const uint32_t target_vp = state.lease ? Word(ram, r.area + 0x40) : getRegU32(ctx, 4);
    RecordSubmission(ram, state, r, target_vp);
}

void PrepareAreaLineTables(uint8_t* ram, const Relocation& r) {
    if (!r.base || !r.area) return;
    const uint32_t lp1 = r.base + kArenaAreaLinePtrsOffset;
    const uint32_t lp2 = r.base + kArenaAreaLinePtrs2Offset;
    const uint32_t h_bytes = r.target_h * 4u;
    for (uint32_t y = 0; y < r.target_h; ++y) {
        const uint32_t line_addr = r.base + r.layout.visible_offset + y * r.layout.pitch;
        WriteWord(ram, lp1 + y * 4u, line_addr);
        WriteWord(ram, lp2 + y * 4u, line_addr);
    }
    WriteWord(ram, r.area + 0x08, r.base + 16u);
    WriteWord(ram, r.area + 0x68, r.base + 16u);
    WriteWord(ram, r.area + 0x20, r.layout.pitch);
    WriteWord(ram, r.area + 0xC8, lp1);
    WriteWord(ram, r.area + 0xCC, lp1);
    WriteWord(ram, r.area + 0xD0, lp2);
    WriteWord(ram, r.area + 0xF38, h_bytes);
    WriteWord(ram, r.area + 0xF3C, h_bytes);
}

void HandleAreaLineTableHook(uint8_t* ram, R5900Context* ctx, State& state, size_t slot) {
    if (!state.lease || ctx->pc != entries[slot]) return;
    const auto& r = state.relocation;
    const uint32_t area = getRegU32(ctx, 4);
    if (!r.base || !r.area || area != r.area) return;
    const uint32_t h_bytes = Word(ram, area + 0x1C) * 4u;
    const uint32_t lp1 = r.base + kArenaAreaLinePtrsOffset;
    const uint32_t lp2 = r.base + kArenaAreaLinePtrs2Offset;
    WriteWord(ram, area + 0x08, r.base + 16u);
    WriteWord(ram, area + 0x68, r.base + 16u);
    WriteWord(ram, area + 0x20, r.layout.pitch);
    WriteWord(ram, area + 0xC8, lp1);
    WriteWord(ram, area + 0xCC, lp1);
    WriteWord(ram, area + 0xD0, lp2);
    WriteWord(ram, area + 0xF38, h_bytes);
    WriteWord(ram, area + 0xF3C, h_bytes);
}

size_t ClaimVSpanSlot(State& state, uint32_t vspan) {
    for (size_t i = 0; i < kArenaVSpanSlotCount; ++i) {
        if (state.vspan_owners[i] == vspan) return i;
    }
    for (size_t i = 0; i < kArenaVSpanSlotCount; ++i) {
        if (state.vspan_owners[i] == 0) {
            state.vspan_owners[i] = vspan;
            return i;
        }
    }
    state.vspan_owners[0] = vspan;
    return 0;
}

bool HandleVSpanInitHook(uint8_t* ram, R5900Context* ctx, State& state) {
    if (!state.lease || ctx->pc != entries[9] || !state.relocation.base) return false;
    const uint32_t vspan = getRegU32(ctx, 4);
    const uint32_t width = getRegU32(ctx, 5);
    const uint32_t height = getRegU32(ctx, 6);
    if (!vspan || height > kMaxArenaScanlines) return false;
    const size_t slot = ClaimVSpanSlot(state, vspan);
    const uint32_t slot_base =
        state.relocation.base + kArenaVSpanArraysOffset + static_cast<uint32_t>(slot) * kArenaVSpanSlotStride;
    std::memset(Bytes(ram, slot_base, kArenaVSpanSlotStride), 0, kArenaVSpanSlotStride);
    std::memset(Bytes(ram, vspan + 0x2Cu, 0x1C04u), 0, 0x1C04u);
    WriteWord(ram, vspan + 0x00u, slot_base + 0x0000u);
    WriteWord(ram, vspan + 0x04u, slot_base + 0x2000u);
    WriteWord(ram, vspan + 0x08u, slot_base + 0x4000u);
    WriteWord(ram, vspan + 0x0Cu, slot_base + 0x6000u);
    WriteWord(ram, vspan + 0x10u, slot_base + 0x8000u);
    WriteWord(ram, vspan + 0x14u, height);
    WriteWord(ram, vspan + 0x18u, width);
    if (static_cast<int32_t>(width) > 0 && static_cast<int32_t>(height) > 0) {
        WriteWord(ram, vspan + 0x1Cu, 0u);
        WriteWord(ram, vspan + 0x20u, 0u);
        WriteWord(ram, vspan + 0x24u, height - 1u);
        WriteWord(ram, vspan + 0x28u, width - 1u);
    } else {
        WriteWord(ram, vspan + 0x1Cu, 0u);
        WriteWord(ram, vspan + 0x20u, 0u);
        WriteWord(ram, vspan + 0x24u, 0u);
        WriteWord(ram, vspan + 0x28u, 0u);
    }
    ctx->pc = getRegU32(ctx, 31);
    return true;
}

void SpliceVSpanLineToFreeList(uint8_t* ram, uint32_t head, uint32_t tail_hint) {
    uint32_t curr = head;
    uint32_t count = 1u;
    while (curr != tail_hint && count < 8192u) {
        const uint32_t next = Word(ram, curr);
        if (!next) break;
        curr = next;
        ++count;
    }
    const uint32_t old_free = Word(ram, 0xCD7310u);
    WriteWord(ram, curr, old_free);
    WriteWord(ram, 0xCD7310u, head);
    WriteWord(ram, 0xCD7318u, Word(ram, 0xCD7318u) - count);
}

bool HandleVSpanResetHook(uint8_t* ram, R5900Context* ctx, State& state) {
    if (!state.lease || ctx->pc != entries[10] || !state.relocation.base) return false;
    const uint32_t vspan = getRegU32(ctx, 4);
    if (!vspan) return false;
    const uint32_t head_arr = Word(ram, vspan + 0x00u);
    const uint32_t arena_start = state.relocation.base + kArenaVSpanArraysOffset;
    const uint32_t arena_end =
        arena_start + static_cast<uint32_t>(kArenaVSpanSlotCount) * kArenaVSpanSlotStride;
    if (head_arr < arena_start || head_arr >= arena_end) return false;
    const uint32_t tail_arr = Word(ram, vspan + 0x04u);
    for (uint32_t i = 0; i < kMaxArenaScanlines; ++i) {
        const uint32_t head = Word(ram, head_arr + i * 4u);
        if (!head) continue;
        const uint32_t tail = Word(ram, tail_arr + i * 4u);
        SpliceVSpanLineToFreeList(ram, head, tail);
    }
    std::memset(Bytes(ram, head_arr, kArenaVSpanSlotStride), 0, kArenaVSpanSlotStride);
    ctx->pc = getRegU32(ctx, 31);
    return true;
}

bool HandlePanelRowTableHook(uint8_t* ram, R5900Context* ctx, State& state) {
    if (!state.lease || ctx->pc != entries[11]) return false;
    const auto& r = state.relocation;
    const uint32_t panel = getRegU32(ctx, 4);
    if (!r.base || !r.panel || panel != r.panel) return false;
    const uint32_t area = Word(ram, panel + 0x1C5Cu);
    if (!area) return false;
    const uint32_t table_addr = r.base + kArenaPanelLinePtrsOffset;
    WriteWord(ram, panel + 0x1C64u, table_addr);
    const uint32_t area_lines = Word(ram, area + 0xC8u);
    const uint32_t rows = std::min<uint32_t>(Word(ram, panel + 0x18u), kMaxArenaScanlines);
    if (area_lines && rows > 0) {
        std::memcpy(Bytes(ram, table_addr, rows * 4u),
                    Bytes(ram, area_lines, rows * 4u), rows * 4u);
    }
    ctx->pc = getRegU32(ctx, 31);
    return true;
}

void HandleDrawListAddYHook(R5900Context* ctx, const State& state, size_t slot) {
    if (!state.lease || ctx->pc != entries[slot]) return;
    const auto& r = state.relocation;
    if (!r.base || r.target_h <= 262u) return;
    const int reg = slot == 12 ? 7 : 6;
    const int32_t sort_y = static_cast<int32_t>(getRegU32(ctx, reg));
    const int32_t scaled_bucket =
        std::clamp((sort_y + 132) * 262 / static_cast<int32_t>(r.target_h), 0, 559);
    const int32_t mapped_y = scaled_bucket - 132;
    ctx->r[reg] = _mm_set_epi64x(0, static_cast<uint32_t>(mapped_y));
}

void RasterizeNodePass(uint8_t* ram, R5900Context* ctx, PS2Runtime& runtime,
                       uint32_t node, uint32_t shape, uint16_t flags) {
    if (*Bytes(ram, node + 0x1Fu, 1) != 0) {
        ctx->r[4] = _mm_set_epi64x(0, Word(ram, node + 0x38u));
        ctx->pc = 0x1820c0u;
        FUN_001820c0_0x1820c0(ram, ctx, &runtime);
        ctx->r[4] = _mm_set_epi64x(0, Word(ram, node + 0x30u));
        ctx->r[5] = _mm_set_epi64x(0, Word(ram, node + 0x34u));
        ctx->pc = 0x1820a0u;
        FUN_001820a0_0x1820a0(ram, ctx, &runtime);
    } else {
        ctx->r[4] = _mm_set_epi64x(0, 0);
        ctx->pc = 0x1820c0u;
        FUN_001820c0_0x1820c0(ram, ctx, &runtime);
    }
    const int32_t draw_x = static_cast<int16_t>(Half(ram, node + 0x14u));
    const int32_t draw_y = static_cast<int16_t>(Half(ram, node + 0x16u));
    ctx->r[4] = _mm_set_epi64x(0, shape);
    ctx->r[5] = _mm_set_epi64x(0, static_cast<uint32_t>(draw_x));
    ctx->r[6] = _mm_set_epi64x(0, static_cast<uint32_t>(draw_y));
    ctx->r[7] = _mm_set_epi64x(0, flags);
    ctx->r[8] = _mm_set_epi64x(0, Word(ram, node + 0x0Cu));
    ctx->pc = 0x17e350u;
    FUN_0017e350_0x17e350(ram, ctx, &runtime);
}

bool HandleHardwareNodeDrawHook(uint8_t* /*ram*/, R5900Context* /*ctx*/,
                                PS2Runtime& /*runtime*/, State& /*state*/) {
    return false;
}

void RecordMapPanelSubmission(uint8_t* ram, State& state) {
    auto& r = state.relocation;
    if (!r.base || !r.panel || !r.area) return;
    if (state.lease) CheckCanaries(ram, state);

    Snapshot snap;
    snap.generation = r.generation;
    snap.submission = ++state.submissions;
    snap.lifecycle = state.lifecycle;
    snap.palette_calls = state.palettes;
    snap.panel = r.panel;
    snap.area = r.area;
    snap.viewport = Word(ram, r.area + 0x40u);
    snap.source = r.base + r.layout.visible_offset;

    uint32_t palette_slot = 0;
    uint32_t palette_src = 0;
    if (snap.viewport) {
        palette_slot = Word(ram, snap.viewport + 0x14u);
        if (palette_slot && palette_slot >= 0x100000u && palette_slot < 0x2000000u) {
            palette_src = Word(ram, palette_slot + 4u);
        }
    }
    if (!palette_src || palette_src < 0x100000u || palette_src >= 0x2000000u) {
        const uint32_t pal_obj = Word(ram, r.area + 0x00u);
        if (pal_obj && pal_obj >= 0x100000u && pal_obj < 0x2000000u) {
            palette_slot = pal_obj;
            palette_src = pal_obj + 0x5Cu;
        }
    }
    if (!palette_src || palette_src < 0x100000u || palette_src >= 0x2000000u) {
        palette_src = 0x00C5C700u;
    }
    snap.palette_slot = palette_slot;
    snap.palette_source = palette_src;

    PublishSnapshot(ram, state, r, std::move(snap), r.target_w, r.target_h);
}
}
