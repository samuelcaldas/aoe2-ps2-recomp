// Observe both Sony query outcomes without adding a production device test hook.
#include <aoe2/pad/kt_libpad_model.hpp>
#include "fake_pad_backend.hpp"
#include "fake_pad_guest_memory.hpp"
#include <array>

using namespace aoe2::pad;
int query_result = 1;
int query_calls = 0;
extern "C" int __wrap__ZNK4aoe23pad9PadDevice13InfoPressModeEii(const PadDevice*, int port, int slot) {
    if (port != 1 || slot != 0) return -99;
    ++query_calls;
    return query_result;
}

int main() {
    FakePadGuestMemory memory;
    FakePadBackend backend;
    PadDevice device(backend);
    KtLibpadModel model(memory, device);
    constexpr uint32_t slot = 0x1000;
    memory.Store32(slot + slot_field::kPort, 1);
    for (int result : {-1, 0, 1, 2}) {
        query_result = result;
        for (uint32_t initial : {0x3000Cu, 0xB000Cu}) {
            memory.Store32(slot + slot_field::kFlags, initial);
            std::array<uint8_t, 512> expected{}, actual{};
            memory.ReadBytes(slot, expected.data(), expected.size());
            expected[0xB2] = result == 1 ? 11 : 3;
            if (model.ApplyPressMode(slot) != 0) return 1;
            memory.ReadBytes(slot, actual.data(), actual.size());
            if (actual != expected) return 2;
        }
    }
    return query_calls == 8 ? 0 : 3;
}
