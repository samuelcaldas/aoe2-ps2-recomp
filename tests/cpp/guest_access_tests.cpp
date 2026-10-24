#include <aoe2/runtime/guest_access.hpp>
#include <aoe2/pad/kt_libpad_model.hpp>

#include "fake_pad_backend.hpp"
#include "fake_pad_guest_memory.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <type_traits>

static_assert(std::is_same_v<aoe2::pad::PadGuestMemory, aoe2::runtime::GuestAccess>);

namespace {

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    FakePadGuestMemory fake;
    aoe2::runtime::GuestAccess& memory = fake;
    aoe2::pad::FakePadBackend backend;
    aoe2::pad::PadDevice device(backend);
    aoe2::pad::KtLibpadModel model(memory, device);

    memory.Store8(0, 0x12);
    memory.Store16(1, 0x3456);
    memory.Store32(3, 0x789ABCDE);
    Check(memory.Load8(0) == 0x12, "byte access through shared interface");
    Check(memory.Load16(1) == 0x3456, "halfword access through shared interface");
    Check(memory.Load32(3) == 0x789ABCDE, "word access through shared interface");
    Check(memory.Load8(3) == 0xDE, "fake retains little-endian layout");

    const std::array<uint8_t, 3> input{1, 2, 3};
    std::array<uint8_t, 3> output{};
    memory.WriteBytes(8, input.data(), input.size());
    memory.ReadBytes(8, output.data(), output.size());
    Check(output == input, "bulk access through shared interface");

    memory.ReportAssertion("guest_access_tests.cpp", 42, "test assertion");
    Check(fake.assertions.size() == 1, "assertion forwarded to fake");
    const auto& assertion = fake.assertions.front();
    Check(assertion.file == "guest_access_tests.cpp" && assertion.line == 42 &&
          assertion.expression == "test assertion", "assertion payload preserved");
}
