#include <aoe2/mouse/kt_mouse_model.hpp>
#include "fake_pad_guest_memory.hpp"

#include <cstdlib>
#include <iostream>

namespace {
void Check(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}
class EmptyMouseRpc final : public aoe2::mouse::MouseRpc {
public:
    int Info(uint32_t) override { return -1; }
    int Read(int, uint32_t) override { return -1; }
    int DeviceInfo(int, uint32_t) override { return -1; }
};
void TestInitialization() {
    FakePadGuestMemory memory;
    EmptyMouseRpc service;
    aoe2::mouse::KtMouseModel model(memory, service);
    memory.Store8(0x509eff, 0xab);
    memory.Store8(0x50a070, 0xcd);
    model.Init(1);
    Check(memory.Load32(0x50a060) == 0xffffffff, "module semaphore stays disabled");
    Check(memory.Load32(0x50a064) == 1, "mouse count");
    Check(memory.Load32(0x50a068) == 0x509f00, "guest buffer pointer");
    Check(memory.Load16(0x509f02) == 0xffff, "initial disconnected state");
    Check(memory.Load32(0x509f14) == 4, "four ring entries");
    Check(memory.Load8(0x509eff) == 0xab && memory.Load8(0x50a070) == 0xcd, "init boundaries");
    model.Init(0);
    Check(memory.Load32(0x509f14) == 0, "zero mice is accepted without slot initialization");
    model.Init(2);
    Check(memory.Load16(0x509fb0) == 1, "second mouse index");
    Check(memory.assertions.empty(), "binary accepts zero, one and two mice");
    model.Init(3);
    Check(memory.assertions.size() == 1 && memory.assertions[0].line == 100, "count above two asserts");
    Check(memory.assertions[0].expression == "n_mouse <= KT_MOUSE_MAX_NUM", "original initialization assertion");
}
void TestRingLeaves() {
    FakePadGuestMemory memory;
    EmptyMouseRpc service;
    aoe2::mouse::KtMouseModel model(memory, service);
    model.Init(1);
    Check(model.NextIndex(0, 2) == 3 && model.NextIndex(0, 3) == 0, "index wrap");
    Check(model.WriteEntry(0) == 0x509f20 && model.ReadEntry(0) == 0x509f20, "initial ring pointers");
    model.AdvanceWrite(0);
    model.AdvanceRead(0);
    Check(model.WriteEntry(0) == 0x509f44 && model.ReadEntry(0) == 0x509f44, "entry stride is 36 bytes");
    Check(model.PortState(0x509f00) == -1 && model.State(0) == -1, "signed disconnected state");
    memory.Store32(0x509f04, 7);
    Check(model.PortType(0x509f00) == 0 && model.Status(0) == 0, "unallocated type is zero");
    memory.Store16(0x509f02, 1);
    Check(model.PortType(0x509f00) == 7 && model.Status(0) == 7, "allocated type");
    Check(model.IsAllocated(0x509f00), "allocation predicate");
    model.Enable();
    Check(model.Enabled() == 1, "enable flag");
    model.Disable();
    Check(model.Enabled() == 0, "disable flag");
}
void TestQueueAndPorts() {
    FakePadGuestMemory memory;
    EmptyMouseRpc service;
    aoe2::mouse::KtMouseModel model(memory, service);
    model.Init(2);
    Check(model.FindFreePort() == 0x509f00, "first free port");
    Check(model.Allocate(1) == 0 && model.Allocate(2) == 1, "allocate both ports");
    Check(model.Allocate(3) == -1 && model.FindFreePort() == 0, "no free port");
    Check(model.ReadPacket(0, 0x600000) == 0, "empty ring");
    for (int index = 0; index < 5; ++index)
        Check(model.PostEvent(0, index, -2, 127, -128) == 0, "full queue still reports success");
    for (int index = 0; index < 4; ++index) {
        Check(model.ReadPacket(0, 0x600000) == 1, "read queued packet");
        Check(memory.Load32(0x600000) == unsigned(index), "FIFO order and fifth packet dropped");
        Check(memory.Load32(0x600004) == 0xfffffffe, "signed delta");
        Check(memory.Load32(0x60000c) == 0xffffff80, "signed wheel");
    }
    Check(model.ReadPacket(0, 0x600000) == 0, "drained ring");
    Check(model.PostEvent(0, 42, 0, 0, 0) == 0, "write after wrap");
    Check(model.ReadPacket(0, 0x600000) == 1 && memory.Load32(0x600000) == 42, "read after wrap");
    Check(model.SetState(-1, 1) == -1 && model.PostEvent(-1, 0, 0, 0, 0) == -1, "negative mutation index");
    Check(model.SetState(0, 1) == 0, "active state");
    Check(model.ResetPorts(0) == 1 && model.State(0) == 1 && model.State(1) == -1, "reset inactive only");
    Check(model.ResetPorts(1) == 2 && model.State(0) == -1, "forced reset counts disconnected slots too");
    Check(model.PostEvent(0, 0, 0, 0, 0) == -1, "disconnected rejects event");
}
void TestPayloadAndRelease() {
    FakePadGuestMemory memory;
    EmptyMouseRpc service;
    aoe2::mouse::KtMouseModel model(memory, service);
    model.Init(1);
    model.Allocate(1);
    for (int index = 0; index < 9; ++index) memory.Store8(0x600000 + index, index + 10);
    Check(model.SetPayload(0, 0x600000, -1) == 0, "negative payload length clamps to eight");
    Check(model.ReadPayload(0, 0x600100) == 8, "payload length");
    Check(memory.Load32(0x600100) == 1 && memory.Load8(0x60010b) == 17, "payload type and bytes");
    Check(model.ReadPayload(9, 0) == 0 && memory.assertions.empty(), "null payload skips port validation");
    model.Release(0, 2);
    Check(model.State(0) == 0, "mismatched type not released");
    model.PostEvent(0, 1, 2, 3, 4);
    model.Release(0, 1);
    Check(model.State(0) == -1 && memory.Load32(0x509f20) == 0, "release resets queue");
    Check(model.SetPayload(0, 0x600000, 1) == -1, "unallocated payload rejected");
    Check(model.State(1) == 0 && memory.assertions.back().line == 0x16b, "bounds assertion");
}
class ScriptedMouseRpc final : public aoe2::mouse::MouseRpc {
public:
    explicit ScriptedMouseRpc(FakePadGuestMemory& memory) : memory_(memory) {}
    int Info(uint32_t reply) override {
        memory_.Store8(reply, 1);
        memory_.Store8(reply + 2, 1);
        return 0;
    }
    int DeviceInfo(int, uint32_t reply) override {
        memory_.Store8(reply, 0xa5);
        return 0;
    }
    int Read(int, uint32_t reply) override {
        ++reads;
        memory_.Store8(reply, reads <= 2 ? 4 : 0);
        memory_.Store8(reply + 1, 0x80);
        memory_.Store8(reply + 2, 3);
        memory_.Store8(reply + 3, 0xfe);
        memory_.Store8(reply + 4, 0xff);
        return 0;
    }
    int reads = 0;
private:
    FakePadGuestMemory& memory_;
};
void TestProcessEvents() {
    FakePadGuestMemory memory;
    ScriptedMouseRpc service(memory);
    aoe2::mouse::KtMouseModel model(memory, service);
    model.Init(1);
    model.InitProcessing(2);
    Check(memory.Load32(0x50a088) == 1, "processing clamps two to one");
    model.ProcessEvents();
    Check(service.reads == 3, "reads until empty");
    Check(model.State(0) == 1 && model.Status(0) == 1, "device allocated and active");
    Check(model.ReadPayload(0, 0x600000) == 7 && memory.Load8(0x600004) == 0xa5, "device info payload");
    for (int index = 0; index < 2; ++index) {
        Check(model.ReadPacket(0, 0x600000) == 1, "two source events");
        Check(memory.Load32(0x600000) == 0xffffff80, "buttons sign extend");
        Check(memory.Load32(0x600008) == 0xfffffffe, "dy sign extends");
        Check(memory.Load32(0x60000c) == 0xffffffff, "wheel sign extends");
    }
    Check(model.ReadPacket(0, 0x600000) == 0, "no duplicated event");
    Check(memory.Load8(0x50a1c0) == 0, "consumed report cleared");
}
void TestRecordsAndPadding() {
    FakePadGuestMemory memory;
    EmptyMouseRpc service;
    aoe2::mouse::KtMouseModel model(memory, service);
    model.Init(1);
    const auto before = memory.bytes;
    model.WorkPadding();
    model.DevicePadding();
    Check(before == memory.bytes, "binary alignment returns do not change memory");
    model.InitRecords(0x600000, 2, 0x10);
    Check(memory.Load32(0x600014) == 1 && memory.Load32(0x600018) == 0xffffffff, "record array stride");
    model.Allocate(2);
    memory.Store32(0x600008, 0);
    model.ReconcileRecords(0x600000, 1, 0x10, 1);
    Check(memory.Load32(0x600008) == 0xffffffff, "type mismatch invalidates mapping");
    memory.Store8(0x600100, 0xff);
    model.ApplyInfo(0x600000, 1, 0x10, 1, 0x600100);
    Check(memory.Load32(0x600000) == 0xffffffff, "negative report count skips records");
    model.InitProcessing(-1);
    Check(memory.Load32(0x50a088) == 0xffffffff, "negative processing count preserved");
    const auto initialized = memory.bytes;
    model.ProcessEvents();
    Check(initialized == memory.bytes, "failed info leaves state unchanged");
}
}
int main() {
    TestInitialization();
    TestRingLeaves();
    TestQueueAndPorts();
    TestPayloadAndRelease();
    TestProcessEvents();
    TestRecordsAndPadding();
}
