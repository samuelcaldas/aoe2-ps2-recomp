#pragma once

#include <aoe2/pad/pad_device.hpp>

#include <vector>

namespace aoe2::pad {

struct RumbleCall {
    int port;
    uint8_t small_motor;
    uint8_t large_motor;
};

class FakePadBackend final : public PadBackend {
public:
    HostPadState state{true, 0, 0.0f, 0.0f, 0.0f, 0.0f};
    std::vector<RumbleCall> rumble_calls;

    HostPadState Sample(int) override { return state; }
    void Rumble(int port, uint8_t small_motor, uint8_t large_motor) override {
        rumble_calls.push_back({port, small_motor, large_motor});
    }
};

}
