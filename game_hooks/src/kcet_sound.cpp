// KCET host sound boundary replacements.
// Origin: samuelcaldas/age-of-empires-2-decomp src/kcet/kt_sound.c
//
// Replaces the four SIF RPC client functions that the SDD (Sound Driver Daemon,
// service 0x30) and SDCD (CD audio daemon, service 0x20) would normally call
// into the IOP. Without SPU2 emulation those RPCs never complete; the game
// loops forever in func_0012D888 waiting for SDD_STATUS_READY (4).
//
// These handlers return the values a fully-initialized audio IOP would return:
//   kcet.sound.init        -> 0 (success, both RPC clients bound)
//   kcet.sound.open_stream -> 0 (success, CD audio stream open)
//   kcet.sound.query_volume-> 0 (success, 48-byte response zeroed)
//   kcet.sound.query_status-> 4 (SDD_STATUS_READY; unlocks the poll loop)
//
// ADR 0001: hooks are applied by address from the git-ignored kcet_hooks.ini,
// never by modifying generated C++ or third_party/PS2Recomp.

#include <aoe2/game_hooks/kcet_handlers.hpp>
#include "runtime/ps2_memory.h"

namespace {
constexpr int kRegRa = 31;

void ReturnToCaller(R5900Context* ctx, uint32_t value) {
    setReturnU32(ctx, value);
    ctx->pc = getRegU32(ctx, kRegRa);
}

// func_0012D648: binds SDCD (0x20) and SDD (0x30) RPC clients.
// Returns 0 immediately; no SIF RPC needed on the host.
void SoundInit(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    ReturnToCaller(ctx, 0);
}

// func_0012D7E0: opens an audio stream on CD by RPC cmd 0x10 to SDCD.
// Returns 0 immediately; no disc audio playback on the host.
void SoundOpenStream(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    ReturnToCaller(ctx, 0);
}

// func_0012D840: volume/extended-status query (SDD cmd & 0xFF).
// Returns 0; the 48-byte response buffer in guest RDRAM stays zero-initialized,
// which is a safe no-op for volume and status queries.
void SoundQueryVolume(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    ReturnToCaller(ctx, 0);
}

// func_0012D888: SDD readiness poll (cmd 3).
// Returns SDD_STATUS_READY (4), matching the response a real SPU2 DMA interrupt
// would produce. This breaks the 16M-iteration busy-delay loop that otherwise
// stalls Thread 2 indefinitely at pc=0x220FB8.
void SoundQueryStatus(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    constexpr uint32_t kSddStatusReady = 4;
    ReturnToCaller(ctx, kSddStatusReady);
}
}  // namespace

// func_0012D840 (query_volume: SDD cmd & 0xFF) is not separately exported in
// the Ghidra function map and cannot be hooked via replaceFunction.  It is not
// called in the boot stall path, so omitting it does not block progress.

namespace aoe2::game_hooks {
void RegisterSoundHandlers(KcetHandlers& handlers) {
    handlers.emplace("kcet.sound.init", SoundInit);
    handlers.emplace("kcet.sound.open_stream", SoundOpenStream);
    handlers.emplace("kcet.sound.query_status", SoundQueryStatus);
}
}  // namespace aoe2::game_hooks
