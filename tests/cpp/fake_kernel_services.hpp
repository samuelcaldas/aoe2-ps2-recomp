#pragma once

#include <aoe2/kernel/kernel_services.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

class FakeKernelServices final : public aoe2::kernel::KernelServices {
public:
    int CreateSemaphore(int init_count, int max_count, int option) override {
        calls.push_back("create_semaphore " + std::to_string(init_count) + " " + std::to_string(max_count) + " " + std::to_string(option));
        if (fail_create_after >= 0 && successful_creates >= fail_create_after) return -1;
        ++successful_creates;
        return next_id++;
    }
    int DeleteSemaphore(int id) override { calls.push_back("delete " + std::to_string(id)); return id; }
    int SignalSemaphore(int id) override { calls.push_back("signal " + std::to_string(id)); return id; }
    int SignalSemaphoreFromInterrupt(int id) override { calls.push_back("signal_isr " + std::to_string(id)); return id; }
    int PollSemaphore(int id) override {
        calls.push_back("poll " + std::to_string(id));
        auto it = poll_results.find(id);
        if (it == poll_results.end() || it->second.empty()) return 0;
        int result = it->second.front();
        it->second.erase(it->second.begin());
        return result;
    }
    int WaitSemaphore(int id) override { calls.push_back("wait " + std::to_string(id)); return wait_result; }
    int AddIntcHandler(int cause, uint32_t guest_handler) override { calls.push_back("add_intc " + std::to_string(cause) + " " + std::to_string(guest_handler)); return next_id++; }
    int RemoveIntcHandler(int cause, int handler_id) override { calls.push_back("remove_intc " + std::to_string(cause) + " " + std::to_string(handler_id)); return handler_id; }
    void EnableIntc(int cause) override { calls.push_back("enable_intc " + std::to_string(cause)); }
    uint32_t LoadWord(uint32_t guest_address) override { return memory[guest_address]; }
    void StoreWord(uint32_t guest_address, uint32_t value) override { memory[guest_address] = value; }
    std::string LoadString(uint32_t guest_address) override { return strings[guest_address]; }
    void Log(std::string_view message) override { calls.push_back("log " + std::string(message)); }
    void ReportAssertion(std::string_view file, int line, std::string_view expression) override { calls.push_back("assert " + std::string(file) + " " + std::to_string(line) + " " + std::string(expression)); }
    int CreateThread(const ThreadSpec& spec) override { thread_spec = spec; calls.push_back("create_thread"); return next_id++; }
    int StartThread(int id, uint32_t arg) override { calls.push_back("start_thread " + std::to_string(id) + " " + std::to_string(arg)); return start_result_override.value_or(id); }
    int TerminateThread(int id) override { calls.push_back("terminate_thread " + std::to_string(id)); return terminate_result_override.value_or(id); }
    int DeleteThread(int id) override { calls.push_back("delete_thread " + std::to_string(id)); return delete_result_override.value_or(id); }
    int CurrentThreadId() override { calls.push_back("current_thread"); return current_thread_id; }
    int ReferThread(int id, ThreadInfo& out) override { calls.push_back("refer_thread " + std::to_string(id)); out = thread_info; return id; }
    int ChangeThreadPriority(int id, int priority) override { calls.push_back("change_priority " + std::to_string(id) + " " + std::to_string(priority)); return id; }
    int ReferSemaphore(int id, SemaphoreInfo& out, bool from_interrupt) override { calls.push_back("refer_semaphore " + std::to_string(id) + (from_interrupt ? " isr" : "")); out = semaphore_info; return id; }

    std::vector<std::string> calls;
    std::map<uint32_t, uint32_t> memory;
    std::map<uint32_t, std::string> strings;
    std::map<int, std::vector<int>> poll_results;
    int fail_create_after = -1;
    int successful_creates = 0;
    int next_id = 1;
    int wait_result = 0;
    std::optional<int> start_result_override;
    std::optional<int> terminate_result_override;
    std::optional<int> delete_result_override;
    int current_thread_id = 1;
    ThreadSpec thread_spec{};
    ThreadInfo thread_info{};
    SemaphoreInfo semaphore_info{};
};
