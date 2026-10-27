#pragma once
#include <aoe2/runtime/guest_access.hpp>
#include <cstdint>

namespace aoe2::gpu {

struct GpuCommandRecord {
    uint8_t kind{0};
    uint32_t command_addr{0};
    uint32_t handler_addr{0};
};

// Owner-authorized adaptation: samuelcaldas/age-of-empires-2-decomp,
// commit 1a32ed5e948d9a3abc7dceae5faf73d8c2f458ed, src/kcet/kt_gpu.c.
// Packet helpers express the observed guest-memory contract, not GS state.
class KtGpuModel {
public:
    explicit KtGpuModel(runtime::GuestAccess& memory) : m(memory) {}
    void SetFrame1(uint32_t p, uint32_t data) { Set(p, data, 6, true); }
    void SetFrame2(uint32_t p, uint32_t data) { Set(p, data, 20, true); }
    void SetZbuf1(uint32_t p, uint32_t data) { Set(p, data, 22, true); }
    void SetZbuf2(uint32_t p, uint32_t data) { Set(p, data, 0x3b, false); }
    void SetDrawRegisters(uint32_t p, uint32_t data);
    void ResetTestRegister(uint32_t p);
    // Split dispatch lets the runtime suspend between begin and finalize.
    uint8_t ValidateCommand(uint32_t command);
    uint32_t Handler(uint8_t kind);
    GpuCommandRecord InspectCommand(uint32_t command);
    void Begin(uint32_t p);
    void Finalize(uint32_t p);
    void Register(uint32_t p, uint32_t reg, uint64_t value);
private:
    uint64_t Load64(uint32_t p);
    void Store64(uint32_t p, uint64_t value);
    void Tag(uint32_t p);
    void Set(uint32_t p, uint32_t data, uint32_t base, bool selector);
    runtime::GuestAccess& m;
};
}
