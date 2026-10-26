// KCET host sound boundary replacements.
// Origin: samuelcaldas/age-of-empires-2-decomp src/kcet/kt_sound.c
//
// Replaces the three SIF RPC client functions whose guest addresses are
// exported by Ghidra and lie on the boot stall path.
//
// Without SPU2 emulation the SDD (Sound Driver Daemon, service 0x30) RPC
// never completes; the game loops forever in func_0012D888 waiting for
// SDD_STATUS_READY (4).  func_0012D840 (query_volume) is not separately
// exported in the function map (gap 0x0012d83c..0x0012d888) and is omitted.
//
// Hook return values:
//   kcet.sound.init        -> 0 (success: SDCD + SDD clients bound)
//   kcet.sound.open_stream -> 0 (success: CD audio stream open)
//   kcet.sound.query_status-> 4 (SDD_STATUS_READY; unlocks the poll loop)
//
// ADR 0001: hooks are applied by address from the git-ignored kcet_hooks.ini,
// never by modifying generated C++ or third_party/PS2Recomp.

#include "runtime/ps2_memory.h"
#include <aoe2/game_hooks/kcet_handlers.hpp>

namespace {
constexpr int kRegRa = 31;

void ReturnToCaller(R5900Context *ctx, uint32_t value) {
  setReturnU32(ctx, value);
  ctx->pc = getRegU32(ctx, kRegRa);
}

// func_0012D648: binds SDCD (0x20) and SDD (0x30) RPC clients.
void SoundInit(uint8_t *, R5900Context *ctx, PS2Runtime *) {
  ReturnToCaller(ctx, 0);
}

// func_0012D7E0: opens a CD audio stream (SDCD cmd 0x10).
void SoundOpenStream(uint8_t *, R5900Context *ctx, PS2Runtime *) {
  ReturnToCaller(ctx, 0);
}

// func_0012D888: SDD readiness poll (cmd 3).
// Return 0 (ready/done); status 1 and 4 cause the 16M busy-loop in
// func_00220F70.
void SoundQueryStatus(uint8_t *, R5900Context *ctx, PS2Runtime *) {
  constexpr uint32_t kSddStatusReady = 4;
  ReturnToCaller(ctx, 0);
}
} // namespace

namespace aoe2::game_hooks {
void RegisterSoundHandlers(KcetHandlers &handlers) {
  handlers.emplace("kcet.sound.init", SoundInit);
  handlers.emplace("kcet.sound.open_stream", SoundOpenStream);
  handlers.emplace("kcet.sound.query_status", SoundQueryStatus);
}
} // namespace aoe2::game_hooks
