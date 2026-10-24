// Native replacement of the KCET kernel module (semaphores, threads, vsync).
//
// The logic lives in kernel/ (ported from the decomp, src/kcet/kt_kernel.c); this file only adapts it to
// the EeScheduler and registers one handler per function of the module. Switch: "kernel=on" in
// native_modules.ini next to the ELF; off (or missing file) installs nothing.
//
// Scheduling contract (same as the generated syscall stubs): pc is set to the return address BEFORE any
// call that may block or reschedule, because those unwind the host frames with EeDispatcherTransfer and
// the thread later resumes at pc. Non-interrupt signals therefore never reschedule in the middle of a
// function; the single reschedule point is the end of the handler.
#include <aoe2/game_hooks/native_module_switches.hpp>
#include <aoe2/kernel/thread_model.hpp>
#include <aoe2/kernel/vsync_model.hpp>

#include "game_overrides.h"
#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "runtime/ps2_memory.h"

#include <array>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>
#include <variant>
#include <string>

namespace {

using aoe2::kernel::KernelServices;
using aoe2::kernel::ThreadModel;
using aoe2::kernel::VsyncCallbacks;
using aoe2::kernel::VsyncModel;

constexpr int kKeOk = 0;
constexpr int kKeError = -1;
constexpr int kKeUnknownThreadId = -407;
constexpr int kKeUnknownSemaphoreId = -408;
constexpr int kKeNotDormant = -414;

constexpr uint32_t kGpGlobal = 0x004EA7C0;        // D_4EA7C0, passed as the thread gp
constexpr uint32_t kStatusTable = 0x00490740;     // D_490740, six {status, name pointer} pairs
constexpr int kRegV0 = 2, kRegA0 = 4, kRegGp = 28, kRegSp = 29, kRegRa = 31;

#define KERNEL_ENTRY(name, address) constexpr uint32_t kAddress_##name = address;
#define KERNEL_DEFERRED(address, reason)
#include "kernel_entries.def"
#undef KERNEL_ENTRY
#undef KERNEL_DEFERRED

int ThreadStatusCode(EeThreadStatus status) {
    switch (status) {
    case EeThreadStatus::Running: return 0x01;
    case EeThreadStatus::Ready: return 0x02;
    case EeThreadStatus::Waiting: return 0x04;
    case EeThreadStatus::WaitingSuspended: return 0x0c;
    case EeThreadStatus::Suspended: return 0x08;
    case EeThreadStatus::Dormant: return 0x10;
    }
    return 0x10;
}

uint32_t WaitTypeCode(EeWaitReason reason) {
    switch (reason) {
    case EeWaitReason::Sleep: return 1;
    case EeWaitReason::Semaphore: return 2;
    case EeWaitReason::EventFlag: return 3;
    case EeWaitReason::VSync: return 4;
    case EeWaitReason::External:
    case EeWaitReason::Mpeg: return 5;
    case EeWaitReason::None: return 0;
    }
    return 0;
}

uint32_t WaitObjectId(const GuestThread& thread) {
    if (thread.wait.reason == EeWaitReason::Semaphore) return static_cast<uint32_t>(std::get<EeSemaphoreWait>(thread.wait.payload).id);
    if (thread.wait.reason == EeWaitReason::EventFlag) return static_cast<uint32_t>(std::get<EeEventFlagWait>(thread.wait.payload).id);
    return 0;
}

class SchedulerKernelServices final : public KernelServices {
public:
    explicit SchedulerKernelServices(PS2Runtime& runtime) : runtime_(runtime) {}

    // Called at the start of every handler; mirrors what each upstream syscall does first.
    void Bind(uint8_t* rdram, R5900Context* ctx) {
        rdram_ = rdram;
        ctx_ = ctx;
        runtime_.eeScheduler().bindMainContextForSyscall(*ctx, rdram);
    }

    // True when a non-interrupt operation may have requested a reschedule; consumed by the handler.
    bool TakeReschedulePending() { return std::exchange(reschedule_pending_, false); }

    int CreateSemaphore(int init_count, int max_count, int option) override {
        return ee().createSemaphore(init_count, max_count, 0u, static_cast<uint32_t>(option));
    }
    int DeleteSemaphore(int id) override {
        reschedule_pending_ = true;
        return ee().deleteSemaphore(id, false);
    }
    int SignalSemaphore(int id) override {
        reschedule_pending_ = true;
        return ee().signalSemaphore(id, false);
    }
    int SignalSemaphoreFromInterrupt(int id) override { return ee().signalSemaphore(id, true); }
    int PollSemaphore(int id) override { return ee().pollSemaphore(id); }
    int WaitSemaphore(int id) override {
        ee().waitSemaphore(id); // may throw EeDispatcherTransfer; pc was already set by the handler
        return static_cast<int>(getRegU32(ctx_, kRegV0));
    }

    int AddIntcHandler(int cause, uint32_t guest_handler) override {
        return ee().addIrqHandler(false, static_cast<uint32_t>(cause), guest_handler, true, 0u,
                                  getRegU32(ctx_, kRegGp), getRegU32(ctx_, kRegSp));
    }
    // Port of the decomp's func_00134DE8 (kt_mouse.c): disable the cause, remove the handler, and re-enable the
    // cause only when both the removal result and the disable result are non-zero (the upstream syscalls return
    // zero for both, so the cause stays disabled, exactly as in the original run).
    int RemoveIntcHandler(int cause, int handler_id) override {
        const int was_enabled = ee().setIrqCauseEnabled(false, static_cast<uint32_t>(cause), false);
        const int result = ee().removeIrqHandler(false, static_cast<uint32_t>(cause), handler_id);
        if (result != 0 && was_enabled != 0) EnableIntc(cause);
        return result;
    }
    void EnableIntc(int cause) override { ee().setIrqCauseEnabled(false, static_cast<uint32_t>(cause), true); }

    uint32_t LoadWord(uint32_t address) override {
        const uint8_t* source = getMemPtr(rdram_, address);
        uint32_t value = 0;
        if (source != nullptr) std::memcpy(&value, source, sizeof(value));
        return value;
    }
    void StoreWord(uint32_t address, uint32_t value) override {
        uint8_t* destination = getMemPtr(rdram_, address);
        if (destination != nullptr) std::memcpy(destination, &value, sizeof(value));
    }
    std::string LoadString(uint32_t address) override {
        std::string text;
        const char* source = reinterpret_cast<const char*>(getMemPtr(rdram_, address));
        for (std::size_t i = 0; source != nullptr && source[i] != '\0' && i < 256; ++i) text.push_back(source[i]);
        return text;
    }

    void Log(std::string_view message) override { std::cerr << "[native-kernel] " << message << std::endl; }
    // The original calls a guest assertion reporter; here it is log-only (open item in the plan).
    void ReportAssertion(std::string_view file, int line, std::string_view expression) override {
        std::cerr << "[native-kernel] ASSERT " << file << ':' << line << ' ' << expression << std::endl;
    }

    int CreateThread(const ThreadSpec& spec) override {
        const int stack_size = static_cast<int>(spec.stack_size);
        if (stack_size < 0) return kKeError;
        const bool stack_resolves = spec.stack == 0u || (getMemPtr(rdram_, spec.stack) != nullptr &&
                                                         (stack_size == 0 || getMemPtr(rdram_, spec.stack + static_cast<uint32_t>(stack_size) - 1u) != nullptr));
        if (!stack_resolves) return kKeError;
        return ee().createThread(EeThreadCreateParams{0u, spec.entry, spec.stack, spec.stack_size, spec.gp,
                                                      static_cast<int>(spec.init_priority), spec.option});
    }
    // Mirrors ps2_syscalls::StartThread (Thread.cpp): checks, stack allocation, id on success.
    int StartThread(int id, uint32_t arg) override {
        GuestThread* target = ee().thread(id);
        if (target == nullptr) return kKeUnknownThreadId;
        if (target->status != EeThreadStatus::Dormant) return kKeNotDormant;
        if (!runtime_.hasFunction(target->entry)) return kKeError;
        if (target->stack == 0u && target->stackSize != 0u) {
            target->stack = runtime_.guestMalloc(target->stackSize, 16u);
            if (target->stack == 0u) return kKeError;
            target->ownsStack = true;
        }
        const int result = ee().startThread(id, arg, *ctx_, false);
        reschedule_pending_ = true;
        return result == kKeOk ? id : result;
    }
    int TerminateThread(int id) override {
        uint32_t owned_stack = 0;
        const int result = ee().terminateThread(id, owned_stack, false);
        if (owned_stack != 0u) runtime_.guestFree(owned_stack);
        return result;
    }
    int DeleteThread(int id) override {
        uint32_t owned_stack = 0;
        const int result = ee().deleteThread(id, owned_stack);
        if (result == kKeOk) runtime_.removeEeExitHandlers(id);
        if (owned_stack != 0u) runtime_.guestFree(owned_stack);
        return result;
    }
    int CurrentThreadId() override { return ee().currentThreadId(); }
    int ReferThread(int id, ThreadInfo& out) override {
        if (id == 0) id = ee().currentThreadId();
        const GuestThread* thread = ee().thread(id);
        if (thread == nullptr) return kKeUnknownThreadId;
        out = ThreadInfo{static_cast<uint32_t>(ThreadStatusCode(thread->status)), thread->entry, thread->stack, thread->stackSize,
                         thread->gp, static_cast<uint32_t>(thread->initialPriority), static_cast<uint32_t>(thread->currentPriority),
                         thread->attr, thread->option, WaitTypeCode(thread->wait.reason), WaitObjectId(*thread), thread->wakeupCount};
        return kKeOk;
    }
    int ChangeThreadPriority(int id, int priority) override {
        int old_priority = 0;
        reschedule_pending_ = true;
        return ee().changePriority(id, priority, false, old_priority);
    }
    int ReferSemaphore(int id, SemaphoreInfo& out, bool) override {
        const EeSemaphore* semaphore = ee().semaphore(id);
        if (semaphore == nullptr) return kKeUnknownSemaphoreId;
        out = SemaphoreInfo{static_cast<uint32_t>(semaphore->count), static_cast<uint32_t>(semaphore->initCount),
                            static_cast<uint32_t>(semaphore->maxCount), static_cast<uint32_t>(semaphore->waiters.size()), semaphore->option};
        return kKeOk;
    }

private:
    EeScheduler& ee() { return runtime_.eeScheduler(); }

    PS2Runtime& runtime_;
    uint8_t* rdram_ = nullptr;
    R5900Context* ctx_ = nullptr;
    bool reschedule_pending_ = false;
};

struct NativeKernel {
    explicit NativeKernel(PS2Runtime& runtime)
        : services(runtime),
          vsync(services, VsyncCallbacks{kAddress_vblank_start_callback, kAddress_vblank_end_callback}),
          threads(services, kGpGlobal, kStatusTable) {}
    SchedulerKernelServices services;
    VsyncModel vsync;
    ThreadModel threads;
};

std::unique_ptr<NativeKernel> g_kernel;

enum Slot : std::size_t {
#define KERNEL_ENTRY(name, address) kSlot_##name,
#define KERNEL_DEFERRED(address, reason)
#include "kernel_entries.def"
#undef KERNEL_ENTRY
#undef KERNEL_DEFERRED
    kSlotCount
};

std::array<std::atomic<uint64_t>, kSlotCount> g_call_counts{};

bool ShouldLog(uint64_t call_number) { return call_number <= 3 || (call_number & (call_number - 1)) == 0; }

int Arg(R5900Context* ctx, int reg) { return static_cast<int>(getRegU32(ctx, reg)); }
uint32_t UArg(R5900Context* ctx, int reg) { return getRegU32(ctx, reg); }

// Runs one native function. `returns_value` is false for void functions. `may_reschedule` is false for
// interrupt callbacks, which never reschedule (their signals use the interrupt-safe variant anyway).
template <typename Body>
void Run(std::size_t slot, const char* name, uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime,
         bool returns_value, bool may_reschedule, Body body) {
    const uint64_t call_number = ++g_call_counts[slot];
    if (ShouldLog(call_number))
        std::cerr << "[native-kernel] " << name << " n=" << call_number << " a0=0x" << std::hex << getRegU32(ctx, kRegA0)
                  << std::dec << std::endl;
    NativeKernel& kernel = *g_kernel;
    kernel.services.Bind(rdram, ctx);
    const uint32_t return_address = getRegU32(ctx, kRegRa);
    ctx->pc = return_address; // resume point if the body blocks
    const int result = body(kernel, ctx);
    if (returns_value) setReturnU32(ctx, static_cast<uint32_t>(result));
    ctx->pc = return_address;
    const bool reschedule_pending = kernel.services.TakeReschedulePending();
    if (reschedule_pending && may_reschedule) runtime->eeScheduler().transferIfRequested(false);
}

#define KERNEL_HANDLER(name, returns_value, may_reschedule, ...)                                                   \
    void Handler_##name(uint8_t* rdram, R5900Context* ctx, PS2Runtime* runtime) {                                  \
        Run(kSlot_##name, #name, rdram, ctx, runtime, returns_value, may_reschedule,                               \
            [](NativeKernel& k, R5900Context* c) -> int { (void)c; __VA_ARGS__ });                                 \
    }

KERNEL_HANDLER(thread_priority_current, true, true, return k.threads.CurrentPriority();)
KERNEL_HANDLER(thread_priority, true, true, return k.threads.Priority(Arg(c, 4));)
KERNEL_HANDLER(thread_priority_change, true, true, return k.threads.ChangeCurrentPriority(Arg(c, 4));)
KERNEL_HANDLER(thread_terminate_delete, true, true, return k.threads.TerminateAndDelete(Arg(c, 4));)
KERNEL_HANDLER(sema_create, true, true, return k.vsync.CreateSemaphore(Arg(c, 4), Arg(c, 5), Arg(c, 6));)
KERNEL_HANDLER(thread_create, true, true,
               return k.threads.Create(UArg(c, 4), UArg(c, 5), Arg(c, 6), Arg(c, 7), Arg(c, 8));)
KERNEL_HANDLER(thread_start, true, true, return k.threads.Start(Arg(c, 4), UArg(c, 5));)
KERNEL_HANDLER(thread_create_start, true, true,
               return k.threads.CreateAndStart(UArg(c, 4), UArg(c, 5), Arg(c, 6), Arg(c, 7), UArg(c, 8), Arg(c, 9));)
KERNEL_HANDLER(sema_drain_report, true, true, return k.vsync.DrainSemaphoreReporting(Arg(c, 4));)
KERNEL_HANDLER(sema_drain, true, true, return k.vsync.DrainSemaphore(Arg(c, 4));)
KERNEL_HANDLER(sema_signal, true, true, return k.vsync.SignalSemaphore(Arg(c, 4));)
KERNEL_HANDLER(sema_signal_isr, true, true, return k.vsync.SignalSemaphoreFromInterrupt(Arg(c, 4));)
KERNEL_HANDLER(sema_delete, true, true, return k.vsync.DeleteSemaphore(Arg(c, 4));)
KERNEL_HANDLER(sema_wait, true, true, return k.vsync.WaitSemaphore(Arg(c, 4));)
KERNEL_HANDLER(vsync_sema_0, true, true, return k.vsync.VsyncSemaphore(0);)
KERNEL_HANDLER(vsync_sema_1, true, true, return k.vsync.VsyncSemaphore(1);)
KERNEL_HANDLER(vsync_sema_2, true, true, return k.vsync.VsyncSemaphore(2);)
KERNEL_HANDLER(vsync_sema_3, true, true, return k.vsync.VsyncSemaphore(3);)
KERNEL_HANDLER(vblank_start_callback, true, false, return k.vsync.OnVblankStart();)
KERNEL_HANDLER(vblank_end_callback, true, false, return k.vsync.OnVblankEnd();)
KERNEL_HANDLER(vsync_remove_handlers, false, true, k.vsync.RemoveHandlers(); return 0;)
KERNEL_HANDLER(vsync_delete_semas, false, true, k.vsync.DeleteVsyncSemaphores(); return 0;)
KERNEL_HANDLER(vsync_init_range, true, true, return k.vsync.InitializeRange(Arg(c, 4), Arg(c, 5));)
KERNEL_HANDLER(vsync_register_start, true, true, return k.vsync.RegisterVblankStart();)
KERNEL_HANDLER(vsync_register_end, true, true, return k.vsync.RegisterVblankEnd();)
KERNEL_HANDLER(vsync_init, true, true, return k.vsync.Init(Arg(c, 4));)
KERNEL_HANDLER(vsync_shutdown, false, true, k.vsync.Shutdown(); return 0;)
KERNEL_HANDLER(vsync_wait_pair, true, true, return k.vsync.WaitPair(Arg(c, 4), Arg(c, 5));)
KERNEL_HANDLER(vsync_wait_start, true, true, return k.vsync.WaitVblankStart();)
KERNEL_HANDLER(vsync_wait_end, true, true, return k.vsync.WaitVblankEnd();)
KERNEL_HANDLER(thread_status, true, true, return k.threads.Status(Arg(c, 4));)
KERNEL_HANDLER(thread_dump, false, true, k.threads.Dump(Arg(c, 4)); return 0;)
KERNEL_HANDLER(thread_status_name, true, true, return static_cast<int>(k.threads.StatusNameAddress(Arg(c, 4)));)
KERNEL_HANDLER(thread_dump_all, false, true, k.threads.DumpAll(); return 0;)
KERNEL_HANDLER(sema_dump_isr, false, true, k.vsync.DumpSemaphore(Arg(c, 4), true); return 0;)
KERNEL_HANDLER(sema_dump_all_isr, false, true, k.vsync.DumpAllSemaphores(true); return 0;)
KERNEL_HANDLER(sema_dump, false, true, k.vsync.DumpSemaphore(Arg(c, 4), false); return 0;)
KERNEL_HANDLER(sema_dump_all, false, true, k.vsync.DumpAllSemaphores(false); return 0;)

struct Entry {
    uint32_t address;
    PS2Runtime::RecompiledFunction handler;
    const char* name;
};

const std::array<Entry, kSlotCount> kEntries = {{
#define KERNEL_ENTRY(name, address) {address, &Handler_##name, #name},
#define KERNEL_DEFERRED(address, reason)
#include "kernel_entries.def"
#undef KERNEL_ENTRY
#undef KERNEL_DEFERRED
}};

aoe2::game_hooks::NativeModuleSwitches LoadSwitches() {
    const auto path = PS2Runtime::getIoPaths().elfDirectory / "native_modules.ini";
    std::ifstream file(path);
    if (!file.is_open()) return aoe2::game_hooks::NativeModuleSwitches::AllOff();
    return aoe2::game_hooks::NativeModuleSwitches::Parse(file);
}

void install_native_kernel(PS2Runtime& runtime) {
    if (!LoadSwitches().Enabled("kernel")) {
        std::cout << "[aoe2] native kernel: off\n";
        return;
    }
    for (const Entry& entry : kEntries) {
        if (runtime.lookupFunction(entry.address) == nullptr)
            throw std::runtime_error(std::string("Native kernel: no guest function for ") + entry.name);
    }
    g_kernel = std::make_unique<NativeKernel>(runtime);
    for (const Entry& entry : kEntries) {
        if (!runtime.replaceFunction(entry.address, entry.handler))
            throw std::runtime_error(std::string("Native kernel: cannot replace ") + entry.name);
    }
    std::cout << "[aoe2] native kernel: " << kEntries.size() << " functions replaced\n";
}

} // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native kernel", "AOK.ELF", 0, 0, install_native_kernel)
