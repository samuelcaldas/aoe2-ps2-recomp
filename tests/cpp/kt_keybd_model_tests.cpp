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
class EmptyKeyboardRpc final : public aoe2::keyboard::KeyboardRpc {
public:
    int Info(uint32_t) override { return -1; }
    int Read(int, uint32_t) override { return -1; }
    int DeviceInfo(int, uint32_t) override { return -1; }
    int SetLed(int, int) override { return -1; }
    int SetCapsType(int, int) override { return -1; }
};
void TestInitialization() {
    FakePadGuestMemory memory;
    EmptyKeyboardRpc service;
    aoe2::keyboard::KtKeybdModel model(memory, service);
    memory.Store8(0x50a2bf, 0xab);
    memory.Store8(0x50a344, 0xcd);
    model.Init(1);
    Check(memory.Load32(0x50a338) == 0xffffffff, "semaphore disabled");
    Check(memory.Load32(0x50a33c) == 1 && memory.Load32(0x50a340) == 0x50a2c0, "work pointer and count");
    Check(memory.Load16(0x50a2c2) == 0xffff && memory.Load32(0x50a2dc) == 4, "slot initial state and capacity");
    Check(memory.Load8(0x50a2bf) == 0xab && memory.Load8(0x50a344) == 0xcd, "init boundaries");
    model.Init(0);
    Check(memory.Load32(0x50a2dc) == 0 && memory.assertions.empty(), "zero keyboards accepted");
    model.Init(2);
    Check(memory.assertions.size() == 1 && memory.assertions[0].line == 0x66, "two keyboards assert");
}
void TestLeaves() {
    FakePadGuestMemory memory;
    EmptyKeyboardRpc service;
    aoe2::keyboard::KtKeybdModel model(memory, service);
    model.Init(1);
    Check(model.NextIndex(0, 2) == 3 && model.NextIndex(0, 3) == 0, "index wrap");
    Check(model.WriteEntry(0) == 0x50a2e8 && model.ReadEntry(0) == 0x50a2e8, "entry origin");
    model.AdvanceWrite(0);
    model.AdvanceRead(0);
    Check(model.WriteEntry(0) == 0x50a2fc && model.ReadEntry(0) == 0x50a2fc, "twenty-byte stride");
    Check(model.PortState(0x50a2c0) == -1 && model.State(0) == -1, "signed state");
    Check(model.PortType(0x50a2c0) == 0 && model.Status(0) == 0, "unallocated status");
    memory.Store16(0x50a2c2, 0);
    memory.Store32(0x50a2c4, 3);
    Check(model.PortType(0x50a2c0) == 3 && model.Status(0) == 3, "allocated status");
    const auto before = memory.bytes;
    model.WorkPadding();
    model.DevicePadding();
    Check(before == memory.bytes, "alignment returns leave memory unchanged");
}
void TestQueueAndPayload() {
    FakePadGuestMemory memory;
    EmptyKeyboardRpc service;
    aoe2::keyboard::KtKeybdModel model(memory, service);
    model.Init(1);
    Check(model.FindFreePort() == 0x50a2c0 && model.Allocate(1) == 0, "allocate slot");
    Check(model.FindFreePort() == 0 && model.Allocate(1) == -1, "allocation exhausted");
    Check(model.ReadPacket(0, 0x600100) == 0, "empty ring");
    memory.Store8(0x600000, 4);
    memory.Store8(0x600001, 5);
    for (int index = 0; index < 5; ++index)
        Check(model.PostPacket(0, index, 0xff, 0x600000, 2) == 0, "full ring still returns success");
    for (int index = 0; index < 4; ++index) {
        Check(model.ReadPacket(0, 0x600101) == 1, "unaligned packet output");
        Check(memory.Load8(0x600101) == index && memory.Load8(0x600102) == 0xff, "FIFO packet headers");
        Check(memory.Load8(0x600103) == 4 && memory.Load8(0x600104) == 5, "packet payload");
        for (uint32_t byte = 4; byte < 16; ++byte)
            Check(memory.Load8(0x600101 + byte) == 0, "zero padding through sixteen bytes");
    }
    Check(model.ReadPacket(0, 0x600100) == 0, "fifth packet dropped");
    Check(model.PostPacket(0, 0x1ff, 0x180, 0x600000, 0) == 0, "wrapped ring accepts zero-size packet");
    Check(model.ReadPacket(0, 0x600100) == 1 && memory.Load16(0x600100) == 0x80ff, "header truncation");
    Check(model.SetPayload(0, 0x600000, -1) == 0, "negative info size clamps to eight");
    Check(model.ReadPayload(0, 0x600100) == 8 && memory.Load32(0x600100) == 1, "info payload type and length");
    Check(model.ReadPayload(5, 0) == 0 && memory.assertions.empty(), "null output skips bounds check");
    Check(model.SetState(0, 1) == 0 && model.ResetPorts(0) == 0, "active slot retained");
    Check(model.ResetPorts(1) == 1 && model.State(0) == -1, "forced reset");
    Check(model.SetState(-1, 1) == -1 && model.PostPacket(-1, 0, 0, 0, 0) == -1, "negative mutation port");
    Check(model.SetState(0, 1) == -1 && model.SetPayload(0, 0x600000, 1) == -1, "unallocated port rejected");
    model.Allocate(1);
    model.PostPacket(0, 1, 1, 0x600000, 1);
    model.Release(0, 2);
    Check(model.State(0) == 0, "release ignores type mismatch");
    model.Release(0, 1);
    Check(model.State(0) == -1 && memory.Load32(0x50a2e8) == 0, "release clears queue");
}
void TestRequestsAndRecords() {
    FakePadGuestMemory memory;
    EmptyKeyboardRpc service;
    aoe2::keyboard::KtKeybdModel model(memory, service);
    model.Init(1);
    model.Allocate(1);
    model.RequestRepeat(0, 0x123);
    model.RequestLed(0, 0x456);
    Check(static_cast<uint32_t>(model.RepeatRequest(0)) == 0x80000123, "repeat request flag");
    Check(static_cast<uint32_t>(model.LedRequest(0)) == 0x80000456, "led request flag");
    model.ClearRepeatRequest(0, 3);
    model.ClearLedRequest(0, 6);
    Check(model.RepeatRequest(0) == 3 && model.LedRequest(0) == 6, "acknowledged request values");
    Check(model.RepeatRequest(-1) == 0 && model.LedRequest(-1) == 0, "negative request port");
    Check(model.IsAllocated(0x50a2c0), "allocation predicate");
    model.InitRecords(0x600000, 2, 0x10);
    Check(memory.Load32(0x600014) == 1 && memory.Load32(0x600018) == 0xffffffff, "record stride");
    memory.Store32(0x600008, 0);
    model.ReconcileRecords(0x600000, 1, 0x10, 2);
    Check(memory.Load32(0x600008) == 0xffffffff, "changed type invalidates record");
    memory.Store8(0x600100, 0xff);
    model.ApplyInfo(0x600000, 1, 0x10, 1, 0x600100);
    Check(memory.Load32(0x600000) == 0xffffffff, "signed info count skips records");
    model.InitProcessing(2);
    Check(memory.Load32(0x50a388) == 1 && memory.Load32(0x50a384) == 0xffffffff, "processing count clamped");
    Check(memory.Load32(0x50a38c) == 0xffffffff && memory.Load32(0x50a394) == 0xffffffff, "initial record mapping");
    const auto before = memory.bytes;
    model.ProcessEvents();
    Check(before == memory.bytes, "failed info does not mutate state");
}
void TestAssertionsAndMaximumPacket() {
    FakePadGuestMemory memory;
    EmptyKeyboardRpc service;
    aoe2::keyboard::KtKeybdModel model(memory, service);
    model.Init(1);
    model.Allocate(1);
    model.State(1);
    Check(memory.assertions.back().line == 0x114, "state bounds assertion line");
    Check(memory.assertions.back().expression == "keybd_number < W->n_keybd", "state bounds expression");
    model.SetState(1, 0);
    Check(memory.assertions.back().line == 0x217, "set-state assertion line");
    Check(memory.assertions.back().expression == "number < W->n_keybd", "mutation bounds expression");
    for (uint32_t index = 0; index < 14; ++index) memory.Store8(0x600000 + index, index + 1);
    model.PostPacket(0, 1, 2, 0x600000, 13);
    model.ReadPacket(0, 0x600100);
    Check(memory.Load8(0x60010e) == 13 && memory.Load8(0x60010f) == 0, "thirteen-byte payload final pad");
    const auto assertions = memory.assertions.size();
    model.PostPacket(0, 1, 2, 0x600000, 14);
    Check(memory.assertions.size() == assertions + 1 && memory.assertions.back().line == 0x252,
          "fourteen-byte payload asserts before copying");
    Check(memory.assertions.back().expression == "size < KT_KEYBD_KEY_MAX", "payload assertion expression");
}
}
void TestKeyboardProcessing();
int main() {
    TestInitialization();
    TestLeaves();
    TestQueueAndPayload();
    TestRequestsAndRecords();
    TestKeyboardProcessing();
    TestAssertionsAndMaximumPacket();
}
