#pragma once
#include <cstdint>
#include <immintrin.h>
#include <unordered_map>
struct R5900Context { __m128i r[32]{}; uint32_t pc = 0; };
inline uint32_t getRegU32(const R5900Context* ctx, int reg) { return _mm_extract_epi32(ctx->r[reg], 0); }
struct MapArenaLease { uint32_t base = 0, capacity = 0; uint64_t generation = 0; explicit operator bool() const { return generation; } };
class PS2Runtime {
public:
    using RecompiledFunction = void(*)(uint8_t*, R5900Context*, PS2Runtime*);
    std::unordered_map<uint32_t, RecompiledFunction> functions;
    const void* owner = nullptr;
    uint64_t generation = 1;
    RecompiledFunction lookupFunction(uint32_t pc) { return functions[pc]; }
    bool replaceFunction(uint32_t pc, RecompiledFunction fn) { functions[pc] = fn; return true; }
    MapArenaLease claimMapArena(const void* who) {
        if (owner && owner != who) return {};
        owner = who; return {0x3000000, 8*1024*1024, generation};
    }
    bool validateMapArena(const void* who, uint64_t gen) const { return owner == who && generation == gen; }
    bool releaseMapArena(const void* who, uint64_t gen) {
        if (!validateMapArena(who, gen)) return false;
        owner = nullptr; ++generation; return true;
    }
};
