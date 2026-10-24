#include <aoe2/kernel/vsync_model.hpp>
#include "fake_kernel_services.hpp"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool Has(const std::vector<std::string>& calls, const std::string& call) {
    for (const auto& item : calls) {
        if (item == call) return true;
    }
    return false;
}

std::size_t Count(const std::vector<std::string>& calls, const std::string& call) {
    std::size_t result = 0;
    for (const auto& item : calls) result += item == call;
    return result;
}

void init_mode_2_creates_two_semaphores_and_registers_cause_3() {
    FakeKernelServices services;
    services.StoreWord(0x38CEC0, 19);
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    Check(model.Init(2) > 0, "init mode 2 result");
    Check(model.VsyncSemaphore(2) >= 0 && model.VsyncSemaphore(3) >= 0, "mode 2 semaphores");
    Check(model.VsyncSemaphore(0) == 19, "mode 2 preserves other semaphore");
    Check(Has(services.calls, "add_intc 3 22136"), "mode 2 handler");
    Check(Has(services.calls, "enable_intc 3"), "mode 2 enabled");
}

void init_mode_1_registers_cause_2() {
    FakeKernelServices services;
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    Check(model.Init(1) > 0, "init mode 1 result");
    Check(model.VsyncSemaphore(0) >= 0 && model.VsyncSemaphore(1) >= 0, "mode 1 semaphores");
    Check(Has(services.calls, "add_intc 2 4660"), "mode 1 handler");
}

void init_other_modes_register_both() {
    for (int mode : {0, 3, -1}) {
        FakeKernelServices services;
        aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
        Check(model.Init(mode) > 0, "other mode result");
        Check(Has(services.calls, "add_intc 2 4660") && Has(services.calls, "add_intc 3 22136"), "other mode handlers");
    }
}

void second_init_is_a_noop_until_shutdown() {
    FakeKernelServices services;
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    model.Init(2);
    const auto count = services.calls.size();
    model.Init(2);
    Check(services.calls.size() == count, "second init has no calls");
    model.Shutdown();
    Check(model.VsyncSemaphore(0) == -1 && model.VsyncSemaphore(3) == -1, "shutdown clears semaphores");
    Check(model.Init(2) > 0 && Count(services.calls, "add_intc 3 22136") == 2, "init works after shutdown");
}

void create_failure_rolls_back_all() {
    FakeKernelServices services;
    services.fail_create_after = 1;
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    Check(model.Init(1) < 0, "failure result is negative");
    Check(Has(services.calls, "delete 1"), "created semaphore rolled back");
    Check(!Has(services.calls, "add_intc 2 4660"), "handler not registered after failure");
    const auto failed_calls = services.calls.size();
    services.fail_create_after = -1;
    Check(model.Init(1) > 0, "retry after allocation failure succeeds");
    Check(services.calls.size() > failed_calls, "retry performs initialization");
    Check(model.VsyncSemaphore(0) >= 0 && model.VsyncSemaphore(1) >= 0,
          "retry creates semaphores");
    Check(Has(services.calls, "add_intc 2 4660"), "retry registers handler");
}

void wait_vblank_end_signals_then_waits() {
    FakeKernelServices services;
    services.StoreWord(0x38CEC8, 4);
    services.StoreWord(0x38CECC, 5);
    services.wait_result = 27;
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    Check(model.WaitVblankEnd() == 27, "wait result");
    Check(services.calls.size() == 2 && services.calls[0] == "signal 4" && services.calls[1] == "wait 5", "signal then wait");
}

void wait_with_negative_semaphore_logs_and_returns_zero() {
    FakeKernelServices services;
    services.StoreWord(0x38CECC, static_cast<uint32_t>(-1));
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    Check(model.WaitVblankEnd() == 0, "negative semaphore result");
    Check(Has(services.calls, "log invalid vsync semaphore"), "negative semaphore logged");
    Check(!Has(services.calls, "wait -1"), "negative semaphore does not wait");
}

void vblank_callback_drains_poll_and_signals_from_interrupt() {
    FakeKernelServices services;
    services.StoreWord(0x38CEC8, 2);
    services.StoreWord(0x38CECC, 3);
    services.poll_results[2] = {2, 2, 0};
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    Check(model.OnVblankEnd() == 0, "callback result");
    Check(Count(services.calls, "signal_isr 3") == 2, "callback signals count");
    services.calls.clear();
    services.StoreWord(0x38CEC8, static_cast<uint32_t>(-1));
    model.OnVblankEnd();
    Check(services.calls.empty(), "negative callback semaphore ignored");
}

void drain_counts_until_poll_stops() {
    FakeKernelServices services;
    services.poll_results[7] = {7, 7, 7, 0};
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    Check(model.DrainSemaphore(7) == 3, "drain count");
    services.poll_results[8] = {0};
    Check(model.DrainSemaphoreReporting(8) == 0, "zero drain reporting result");
    Check(!Has(services.calls, "log drained semaphore"), "zero drain is not logged");
}

void signal_mismatch_logs() {
    struct MismatchServices final : aoe2::kernel::KernelServices {
        int CreateSemaphore(int, int, int) override { return -1; }
        int DeleteSemaphore(int id) override { return id; }
        int SignalSemaphore(int id) override { return id + 1; }
        int SignalSemaphoreFromInterrupt(int id) override { return id; }
        int PollSemaphore(int) override { return 0; }
        int WaitSemaphore(int id) override { return id; }
        int AddIntcHandler(int, uint32_t) override { return 0; }
        int RemoveIntcHandler(int, int id) override { return id; }
        void EnableIntc(int) override {}
        uint32_t LoadWord(uint32_t) override { return 0; }
        void StoreWord(uint32_t, uint32_t) override {}
        std::string LoadString(uint32_t) override { return {}; }
        void Log(std::string_view message) override { logs.emplace_back(message); }
        void ReportAssertion(std::string_view, int, std::string_view) override {}
        int CreateThread(const ThreadSpec&) override { return -1; }
        int StartThread(int id, uint32_t) override { return id; }
        int TerminateThread(int id) override { return id; }
        int DeleteThread(int id) override { return id; }
        int CurrentThreadId() override { return 1; }
        int ReferThread(int id, ThreadInfo&) override { return id; }
        int ChangeThreadPriority(int id, int) override { return id; }
        int ReferSemaphore(int id, SemaphoreInfo&, bool) override { return id; }
        std::vector<std::string> logs;
    };
    MismatchServices services;
    aoe2::kernel::VsyncModel model(services, {0x1234, 0x5678});
    Check(model.SignalSemaphore(9) == 10, "mismatch return propagated");
    Check(services.logs.size() == 1 && services.logs[0].find("mismatch") != std::string::npos, "mismatch logged");
}
}

int main() {
    init_mode_2_creates_two_semaphores_and_registers_cause_3();
    init_mode_1_registers_cause_2();
    init_other_modes_register_both();
    second_init_is_a_noop_until_shutdown();
    create_failure_rolls_back_all();
    wait_vblank_end_signals_then_waits();
    wait_with_negative_semaphore_logs_and_returns_zero();
    vblank_callback_drains_poll_and_signals_from_interrupt();
    drain_counts_until_poll_stops();
    signal_mismatch_logs();
}
