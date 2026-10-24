#include <aoe2/kernel/thread_model.hpp>
#include "fake_kernel_services.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

namespace {
void Check(bool value, const char* name) {
    if (!value) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
void TestCreatePassesSpecAndGp() {
    FakeKernelServices services;
    aoe2::kernel::ThreadModel model(services, 0x4ea7c0, 0x490740);
    Check(model.Create(0x1000, 0x2000, 0x800, 5, 0) == 1, "create id");
    Check(services.thread_spec.entry == 0x1000 && services.thread_spec.stack == 0x2000 && services.thread_spec.stack_size == 0x800, "spec addresses");
    Check(services.thread_spec.init_priority == 5 && services.thread_spec.option == 0 && services.thread_spec.gp == 0x4ea7c0, "spec values");
}
void TestCreateFailureAssertion() {
    FakeKernelServices services;
    services.next_id = -1;
    aoe2::kernel::ThreadModel model(services, 0, 0);
    Check(model.Create(0, 0, 0, 0, 0) == -1, "create failure result");
    Check(services.calls.back().find("assert ") == 0 && services.calls.back().find(" 147 ") != std::string::npos, "line 0x93 assertion");
}
void TestStartMismatchAssertion() {
    FakeKernelServices services;
    services.start_result_override = 9;
    aoe2::kernel::ThreadModel model(services, 0, 0);
    model.Start(7, 0);
    Check(services.calls.back().find("assert ") == 0 && services.calls.back().find(" 168 ") != std::string::npos, "line 0xa8 assertion");
}
void TestCreateAndStart() {
    FakeKernelServices services;
    aoe2::kernel::ThreadModel model(services, 1, 0);
    Check(model.CreateAndStart(0x10, 0x20, 0x30, 4, 0x55, 2) == 1, "chain id");
    Check(services.calls.back() == "start_thread 1 85", "start called");
}
void TestTerminateDeleteMismatch() {
    FakeKernelServices services;
    aoe2::kernel::ThreadModel model(services, 0, 0);
    Check(model.TerminateAndDelete(3) == 3, "returns id");
    services.terminate_result_override = 8;
    services.delete_result_override = 9;
    Check(model.TerminateAndDelete(3) == 3, "returns id");
    Check(services.calls.size() == 6, "mismatch logs each operation");
}
void TestPriorityCurrent() {
    FakeKernelServices services;
    services.thread_info.current_priority = 9;
    aoe2::kernel::ThreadModel model(services, 0, 0);
    Check(model.CurrentPriority() == 9, "priority");
    Check(services.calls[0] == "current_thread" && services.calls[1] == "refer_thread 1", "lookup sequence");
}
void TestStatusNameReadsGuestTable() {
    FakeKernelServices services;
    constexpr uint32_t table = 0x490740;
    for (uint32_t i = 0; i < 6; ++i) {
        services.memory[table + i * 8] = 10 + i;
        services.memory[table + i * 8 + 4] = 0x1000 + i * 4;
        services.strings[0x1000 + i * 4] = "status" + std::to_string(i);
    }
    aoe2::kernel::ThreadModel model(services, 0, table);
    Check(model.StatusName(13) == std::optional<std::string>("status3"), "status name");
    Check(!model.StatusName(99), "unknown status");
}
void TestDumpAllSkipsUnknownAndCoversIds() {
    FakeKernelServices services;
    services.thread_info.status = 1;
    for (int i = 0; i < 6; ++i) {
        services.memory[0x490740 + i * 8] = static_cast<uint32_t>(i + 1);
        services.memory[0x490744 + i * 8] = 0x1000 + i * 4;
        services.strings[0x1000 + i * 4] = "known";
    }
    aoe2::kernel::ThreadModel model(services, 0, 0x490740);
    model.DumpAll();
    Check(services.calls.size() == 128, "ids 1 through 0x40 with status and dump queries");
}
}
int main() {
    TestCreatePassesSpecAndGp(); TestCreateFailureAssertion(); TestStartMismatchAssertion();
    TestCreateAndStart(); TestTerminateDeleteMismatch(); TestPriorityCurrent();
    TestStatusNameReadsGuestTable(); TestDumpAllSkipsUnknownAndCoversIds();
    std::cout << "thread model tests passed\n";
}
