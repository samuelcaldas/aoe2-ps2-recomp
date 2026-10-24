// Link-time observation of the real model/device boundary, without changing device policy.
#include <aoe2/pad/kt_libpad_model.hpp>
#include "fake_pad_backend.hpp"
#include "fake_pad_guest_memory.hpp"
#include <array>
#include <algorithm>

using namespace aoe2::pad;
std::array<uint8_t, 6> observed{};
extern "C" int __wrap__ZN4aoe23pad9PadDevice11SetActAlignEiiPKh(
    PadDevice*, int, int, const uint8_t* bytes) {
    std::copy_n(bytes, observed.size(), observed.begin());
    return 1;
}

int main() {
    FakePadGuestMemory memory;
    FakePadBackend backend;
    PadDevice device(backend);
    KtLibpadModel model(memory, device);
    constexpr uint32_t slot = 0x1000;
    memory.Store32(slot + slot_field::kFlags, slot_flag::kChanged | slot_flag::kActuatorsLive);
    memory.Store8(0x004E2BD8 + 2, 7);
    memory.Store8(0x004E2BD8 + 3, 9);
    memory.Store8(slot + 0x1C + 4, 7);
    memory.Store8(slot + 0x1C + 5, 9);
    memory.Store8(slot + 0x1C + 6, 2);
    for (int i = 0; i < 4; ++i) memory.Store8(slot + 0x54 + i * 3, 1);
    model.ConfigureAlignment(slot);
    // Three length-two descriptors fit; fourth overflows. Padding stays FF.
    return observed == std::array<uint8_t, 6>{1, 255, 1, 255, 1, 255} ? 0 : 1;
}
