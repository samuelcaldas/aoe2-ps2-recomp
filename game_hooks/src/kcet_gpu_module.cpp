#include "gpu_runtime_adapter.hpp"
#include "gpu_snapshot.hpp"
#include "game_overrides.h"
#include "native_input_module_support.hpp"
#include <iostream>

namespace {
std::array<PS2Runtime::RecompiledFunction, 7> originals{};

// Observe entry boundaries identically in original/native mode. Never log a
// dispatcher return from a C++ call: it may only be a scheduler yield.
template<int Slot>
void Observed(uint8_t* ram, R5900Context* ctx, PS2Runtime* runtime) {
    aoe2::game_hooks::diagnostics::ObserveGpuState(ram, getRegU32(ctx, 4),
        aoe2::game_hooks::gpu::entries[Slot]);
    originals[Slot](ram, ctx, runtime);
}
constexpr std::array<PS2Runtime::RecompiledFunction, 7> observers{
    Observed<0>, Observed<1>, Observed<2>, Observed<3>, Observed<4>, Observed<5>, Observed<6>};

void InstallNativeGpu(PS2Runtime& runtime) {
    using namespace aoe2::game_hooks;
    const bool native = native_input::ReadSwitches().Enabled("gpu");
    if (native) {
        gpu::Install(runtime);
        // No verified semantic HUD replacement exists yet. Preserve the complete
        // guest frame and its input geometry instead of covering live controls.
        std::cout << "[aoe2] presentation: guest framebuffer fallback; native HD HUD unavailable\n";
    }
    if (diagnostics::Enabled()) {
        diagnostics::gpu_snapshot_count = 0;
        for (std::size_t i = 0; i < gpu::entries.size(); ++i) {
            originals[i] = runtime.lookupFunction(gpu::entries[i]);
            if (!originals[i] || !runtime.replaceFunction(gpu::entries[i], observers[i]))
                throw std::runtime_error("GPU observer: incomplete installation");
        }
    }
    std::cout << "[aoe2] native kt_gpu: " << (native ? "installed 7/7" : "off") << '\n';
}
}
PS2_REGISTER_GAME_OVERRIDE("aoe2 native gpu module", "AOK.ELF", 0, 0, InstallNativeGpu)
