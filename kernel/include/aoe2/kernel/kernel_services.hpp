#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace aoe2::kernel {

class KernelServices {
public:
    struct ThreadInfo {
        uint32_t status, entry, stack, stack_size, gp, init_priority;
        uint32_t current_priority, attr, option, wait_type, wait_id, wakeup_count;
    };

    struct ThreadSpec {
        uint32_t entry, stack, stack_size, gp, init_priority, option;
    };

    struct SemaphoreInfo {
        uint32_t current_count, init_count, max_count, wait_threads, option;
    };

    virtual ~KernelServices() = default;

    virtual int CreateSemaphore(int init_count, int max_count, int option) = 0;
    virtual int DeleteSemaphore(int id) = 0;
    virtual int SignalSemaphore(int id) = 0;
    virtual int SignalSemaphoreFromInterrupt(int id) = 0;
    virtual int PollSemaphore(int id) = 0;
    virtual int WaitSemaphore(int id) = 0;

    virtual int AddIntcHandler(int cause, uint32_t guest_handler) = 0;
    virtual int RemoveIntcHandler(int cause, int handler_id) = 0;
    virtual void EnableIntc(int cause) = 0;

    virtual uint32_t LoadWord(uint32_t guest_address) = 0;
    virtual void StoreWord(uint32_t guest_address, uint32_t value) = 0;
    virtual std::string LoadString(uint32_t guest_address) = 0;

    virtual void Log(std::string_view message) = 0;
    virtual void ReportAssertion(std::string_view file, int line, std::string_view expression) = 0;

    virtual int CreateThread(const ThreadSpec& spec) = 0;
    virtual int StartThread(int id, uint32_t arg) = 0;
    virtual int TerminateThread(int id) = 0;
    virtual int DeleteThread(int id) = 0;
    virtual int CurrentThreadId() = 0;
    virtual int ReferThread(int id, ThreadInfo& out) = 0;
    virtual int ChangeThreadPriority(int id, int priority) = 0;
    virtual int ReferSemaphore(int id, SemaphoreInfo& out, bool from_interrupt) = 0;
};

} // namespace aoe2::kernel
