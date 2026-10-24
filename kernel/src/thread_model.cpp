// Origin: samuelcaldas/age-of-empires-2-decomp, commit 1b0496d8da87b3b864e7af8bc929ab45deddc55e, src/kcet/kt_kernel.c
#include <aoe2/kernel/thread_model.hpp>

#include <sstream>

namespace aoe2::kernel {
namespace {
constexpr std::string_view kSourceFile = "src/kcet/kt_kernel.c";
constexpr std::string_view kAssertionExpression = "D_4E2D68";
}
ThreadModel::ThreadModel(KernelServices& services, uint32_t gp_value, uint32_t status_table_address)
    : services_(services), gp_value_(gp_value), status_table_address_(status_table_address) {}
int ThreadModel::CurrentPriority() { return Priority(services_.CurrentThreadId()); }
int ThreadModel::Priority(int id) {
    KernelServices::ThreadInfo info{};
    services_.ReferThread(id, info);
    return static_cast<int>(info.current_priority);
}
int ThreadModel::ChangeCurrentPriority(int priority) { return services_.ChangeThreadPriority(services_.CurrentThreadId(), priority); }
int ThreadModel::TerminateAndDelete(int id) {
    if (services_.TerminateThread(id) != id) services_.Log("thread terminate result mismatch");
    if (services_.DeleteThread(id) != id) services_.Log("thread delete result mismatch");
    return id;
}
int ThreadModel::Create(uint32_t entry, uint32_t stack, int stack_size, int priority, int option) {
    KernelServices::ThreadSpec spec{entry, stack, static_cast<uint32_t>(stack_size), gp_value_, static_cast<uint32_t>(priority), static_cast<uint32_t>(option)};
    int id = services_.CreateThread(spec);
    if (id < 0) services_.ReportAssertion(kSourceFile, 0x93, kAssertionExpression);
    return id;
}
int ThreadModel::Start(int id, uint32_t arg) {
    if (services_.StartThread(id, arg) != id) services_.ReportAssertion(kSourceFile, 0xA8, "D_490610");
    return id;
}
int ThreadModel::CreateAndStart(uint32_t entry, uint32_t stack, int stack_size, int priority, uint32_t arg, int option) {
    int id = Create(entry, stack, stack_size, priority, option);
    Start(id, arg);
    return id;
}
int ThreadModel::Status(int id) { KernelServices::ThreadInfo info{}; services_.ReferThread(id, info); return static_cast<int>(info.status); }
std::optional<std::string> ThreadModel::StatusName(int status) {
    for (uint32_t i = 0; i < 6; ++i) {
        uint32_t base = status_table_address_ + i * 8;
        if (services_.LoadWord(base) == static_cast<uint32_t>(status)) return services_.LoadString(services_.LoadWord(base + 4));
    }
    return std::nullopt;
}
// Port of func_00134A78 and func_00134BB0: the single dump falls back to a placeholder
// for unknown statuses, the dump-all skips them and also prints the wait fields.
std::string ThreadModel::Describe(int id, const KernelServices::ThreadInfo& info, const std::string& status, bool extended) const {
    std::ostringstream out;
    out << "thread " << id << " status=" << status << " entry=" << info.entry << " stack=" << info.stack << " stack_size=" << info.stack_size << " gp=" << info.gp << " init_priority=" << info.init_priority << " current_priority=" << info.current_priority << " attr=" << info.attr;
    if (extended)
        out << " option=" << info.option << " wait_type=" << info.wait_type << " wait_id=" << info.wait_id << " wakeup_count=" << info.wakeup_count;
    return out.str();
}

void ThreadModel::Dump(int id) {
    KernelServices::ThreadInfo info{};
    services_.ReferThread(id, info);
    const auto name = StatusName(static_cast<int>(info.status));
    services_.Log(Describe(id, info, name.value_or("unknown"), false));
}

void ThreadModel::DumpAll() {
    for (int id = 1; id <= 0x40; ++id) {
        KernelServices::ThreadInfo info{};
        services_.ReferThread(id, info);
        const auto name = StatusName(static_cast<int>(info.status));
        if (name) services_.Log(Describe(id, info, *name, true));
    }
}
}
