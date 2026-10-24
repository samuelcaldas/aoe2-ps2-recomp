#pragma once

#include <aoe2/kernel/kernel_services.hpp>
#include <optional>
#include <string>

namespace aoe2::kernel {
class ThreadModel {
public:
    ThreadModel(KernelServices& services, uint32_t gp_value, uint32_t status_table_address);
    int CurrentPriority();
    int Priority(int id);
    int ChangeCurrentPriority(int priority);
    int TerminateAndDelete(int id);
    int Create(uint32_t entry, uint32_t stack, int stack_size, int priority, int option);
    int Start(int id, uint32_t arg);
    int CreateAndStart(uint32_t entry, uint32_t stack, int stack_size, int priority, uint32_t arg, int option);
    int Status(int id);
    std::optional<std::string> StatusName(int status);
    // Guest address of the table's name pointer for the status, 0 when unknown (what the original returns).
    uint32_t StatusNameAddress(int status);
    void Dump(int id);
    void DumpAll();
private:
    std::string Describe(int id, const KernelServices::ThreadInfo& info, const std::string& status, bool extended) const;
    KernelServices& services_;
    uint32_t gp_value_;
    uint32_t status_table_address_;
};
}
