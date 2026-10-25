#include <aoe2/keyboard/kt_keybd_model.hpp>
#include "fake_pad_guest_memory.hpp"
#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}
class ScriptedKeyboardRpc final : public aoe2::keyboard::KeyboardRpc {
public:
    explicit ScriptedKeyboardRpc(FakePadGuestMemory& memory) : memory_(memory) {}
    int Info(uint32_t reply) override {
        memory_.Store8(reply, 1);
        memory_.Store8(reply + 2, 1);
        return info_result;
    }
    int DeviceInfo(int, uint32_t reply) override {
        memory_.Store8(reply, 0xa5);
        return device_result;
    }
    int Read(int, uint32_t reply) override {
        ++reads;
        memory_.Store8(reply, reads == 3 ? 0 : 0x80);
        memory_.Store8(reply + 1, reads <= 3 ? 1 : 0);
        memory_.Store8(reply + 2, 0xff);
        memory_.Store8(reply + 4, 4);
        return read_result;
    }
    int SetLed(int port, int mask) override {
        Check(port == 0, "LED device index");
        led = mask;
        return command_result;
    }
    int SetCapsType(int port, int type) override {
        Check(port == 0, "caps device index");
        caps = type;
        return command_result;
    }
    int reads = 0;
    int led = -1;
    int caps = -1;
    int command_result = -1;
    int info_result = 0;
    int device_result = 0;
    int read_result = 0;
private:
    FakePadGuestMemory& memory_;
};
void TestPressRepeatRelease() {
    FakePadGuestMemory memory;
    ScriptedKeyboardRpc service(memory);
    aoe2::keyboard::KtKeybdModel model(memory, service);
    model.Init(1);
    model.InitProcessing(1);
    model.RequestRepeat(0, 0x123);
    model.RequestLed(0, 0x456);
    model.ProcessEvents();
    Check(service.reads == 4, "drains press, repeat and release within one frame");
    Check(service.led == 0x23 && service.caps == 0x56, "command arguments masked to low byte");
    Check(model.RepeatRequest(0) < 0 && model.LedRequest(0) < 0, "failed commands remain pending");
    Check(model.ReadPayload(0, 0x600100) == 7 && memory.Load8(0x600104) == 0xa5, "device info payload");
    for (int index = 0; index < 3; ++index) {
        Check(model.ReadPacket(0, 0x600100) == 1, "all three events retained");
        Check(memory.Load8(0x600100) == (index == 2 ? 0 : 0x80), "press repeat and release order");
        Check(memory.Load8(0x600101) == 0xff && memory.Load8(0x600102) == 4, "unsigned packet bytes");
        Check(memory.Load8(0x60010f) == 0, "packet tail padded");
    }
    Check(model.ReadPacket(0, 0x600100) == 0, "no duplicate events");
    service.command_result = 0;
    model.ProcessEvents();
    Check(model.RepeatRequest(0) == 0x23 && model.LedRequest(0) == 0x56, "successful commands acknowledged");
    Check(memory.Load8(0x50a4c1) == 0, "drained report count cleared");
}
void TestRpcErrors() {
    FakePadGuestMemory memory;
    ScriptedKeyboardRpc service(memory);
    aoe2::keyboard::KtKeybdModel model(memory, service);
    model.Init(1);
    model.InitProcessing(1);
    service.info_result = -1;
    model.ProcessEvents();
    Check(model.State(0) == -1 && service.reads == 0, "info error skips devices");
    service.info_result = 0;
    service.device_result = -1;
    model.ProcessEvents();
    Check(model.State(0) == 1 && service.reads == 0, "device info error skips reads");
    service.device_result = 0;
    service.read_result = -1;
    model.ProcessEvents();
    Check(service.reads == 1 && model.ReadPacket(0, 0x600000) == 0, "read error queues nothing");
}
}
void TestKeyboardProcessing() {
    TestPressRepeatRelease();
    TestRpcErrors();
}
