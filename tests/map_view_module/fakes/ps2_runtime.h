#pragma once
#include <cstdint>
#include <immintrin.h>
#include <unordered_map>
#include <functional>
#include <optional>
#include "runtime/presentation_frame.h"

struct R5900Context { __m128i r[32]{}; uint32_t pc = 0; };
inline uint32_t getRegU32(const R5900Context* ctx, int reg) { return _mm_extract_epi32(ctx->r[reg], 0); }
inline void setReturnU32(R5900Context* ctx, uint32_t val) { ctx->r[2] = _mm_set_epi64x(0, val); }

struct MapArenaLease { uint32_t base = 0, capacity = 0; uint64_t generation = 0; explicit operator bool() const { return generation; } };

class PS2Runtime {
public:
    using RecompiledFunction = void(*)(uint8_t*, R5900Context*, PS2Runtime*);
    using FrameCompositor = std::function<std::optional<PS2OwnedPresentationFrame>(PS2PresentationFrameView)>;

    std::unordered_map<uint32_t, RecompiledFunction> functions;
    const void* owner = nullptr;
    uint64_t generation = 1;
    unsigned claims = 0, releases = 0, replacements = 0;
    unsigned fail_replacement = 0;
    FrameCompositor compositor;

    RecompiledFunction lookupFunction(uint32_t pc) { return functions[pc]; }
    bool replaceFunction(uint32_t pc, RecompiledFunction fn) { if (++replacements == fail_replacement) return false; functions[pc] = fn; return true; }
    void setFrameCompositor(FrameCompositor c) { compositor = std::move(c); }

    MapArenaLease claimMapArena(const void* who) {
        ++claims;
        if (owner && owner != who) return {};
        owner = who; return {0x3000000, 8*1024*1024, generation};
    }
    bool validateMapArena(const void* who, uint64_t gen) const { return owner == who && generation == gen; }
    bool releaseMapArena(const void* who, uint64_t gen) {
        if (!validateMapArena(who, gen)) return false;
        ++releases;
        owner = nullptr; ++generation; return true;
    }
};
