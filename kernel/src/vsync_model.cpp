// Origin: samuelcaldas/age-of-empires-2-decomp, commit
// af30b177615b34ba35f5c542dc8edfa302da818d (working tree),
// src/kcet/kt_kernel.c.
#include <aoe2/kernel/vsync_model.hpp>

#include <string>

namespace aoe2::kernel {
namespace {
constexpr uint32_t kVsyncSemaphoreTable = 0x38CEC0;
constexpr uint32_t kHandlerTable = 0x38CED0;
constexpr int kSemaphoreCount = 4;
constexpr int kHandlerCount = 2;
} // namespace

VsyncModel::VsyncModel(KernelServices &services, VsyncCallbacks callbacks)
    : services_(services), callbacks_(callbacks) {}

// Port of func_00134210.
int VsyncModel::CreateSemaphore(int init, int max, int option) {
  return services_.CreateSemaphore(init, max, option);
}

// Port of func_00134440.
int VsyncModel::SignalSemaphore(int id) {
  const int result = services_.SignalSemaphore(id);
  ReportSemaphoreResult("signal", id, result);
  return result;
}

// Port of func_00134480.
int VsyncModel::SignalSemaphoreFromInterrupt(int id) {
  const int result = services_.SignalSemaphoreFromInterrupt(id);
  ReportSemaphoreResult("interrupt signal", id, result);
  return result;
}

// Port of func_001344C0.
int VsyncModel::DeleteSemaphore(int id) {
  const int result = services_.DeleteSemaphore(id);
  ReportSemaphoreResult("delete", id, result);
  return result;
}

// Port of func_00134500.
int VsyncModel::WaitSemaphore(int id) { return services_.WaitSemaphore(id); }

// Port of func_001343F8.
int VsyncModel::DrainSemaphore(int id) {
  int count = 0;
  while (services_.PollSemaphore(id) > 0)
    ++count;
  return count;
}

// Port of func_001343A8.
int VsyncModel::DrainSemaphoreReporting(int id) {
  const int count = DrainSemaphore(id);
  if (count > 0)
    services_.Log("drained semaphore");
  return count;
}

// Port of func_00134528, func_00134538, func_00134548, and func_00134558.
int VsyncModel::VsyncSemaphore(int index) {
  if (index < 0 || index >= kSemaphoreCount)
    return -1;
  return static_cast<int>(services_.LoadWord(kVsyncSemaphoreTable +
                                             static_cast<uint32_t>(index * 4)));
}

// Port of func_00134568.
int VsyncModel::OnVblankStart() {
  const int sem0 = VsyncSemaphore(0);
  const int sem1 = VsyncSemaphore(1);
  if (sem0 >= 0)
    while (services_.PollSemaphore(sem0) == sem0)
      SignalSemaphoreFromInterrupt(sem1);
  return 0;
}

// Port of func_001345D0.
int VsyncModel::OnVblankEnd() {
  const int sem2 = VsyncSemaphore(2);
  const int sem3 = VsyncSemaphore(3);
  if (sem2 >= 0)
    while (services_.PollSemaphore(sem2) == sem2)
      SignalSemaphoreFromInterrupt(sem3);
  return 0;
}

// Port of func_00134638.
void VsyncModel::RemoveHandlers() {
  for (int index = 0; index < kHandlerCount; ++index) {
    const uint32_t offset = static_cast<uint32_t>(index * 8);
    const int handler_id =
        static_cast<int>(services_.LoadWord(kHandlerTable + offset + 4));
    const int cause =
        static_cast<int>(services_.LoadWord(kHandlerTable + offset));
    if (handler_id < 0)
      continue;
    services_.RemoveIntcHandler(cause, handler_id);
    services_.StoreWord(kHandlerTable + offset, static_cast<uint32_t>(-1));
    services_.StoreWord(kHandlerTable + offset + 4, static_cast<uint32_t>(-1));
  }
}

// Port of func_001346A8.
void VsyncModel::DeleteVsyncSemaphores() {
  for (int index = 0; index < kSemaphoreCount; ++index) {
    const int sem = VsyncSemaphore(index);
    if (sem < 0)
      continue;
    DeleteSemaphore(sem);
    services_.StoreWord(kVsyncSemaphoreTable + static_cast<uint32_t>(index * 4),
                        static_cast<uint32_t>(-1));
  }
}

// Port of func_00134710.
int VsyncModel::InitializeRange(int start, int end) {
  int id = 0;
  for (int index = start; index < end; ++index) {
    id = CreateSemaphore(0, 0, 0);
    if (id < 0)
      break;
    services_.StoreWord(kVsyncSemaphoreTable + static_cast<uint32_t>(index * 4),
                        static_cast<uint32_t>(id));
  }
  if (id < 0)
    DeleteVsyncSemaphores();
  return id;
}

// Port of func_001347A8.
int VsyncModel::RegisterVblankStart() {
  if (vblank_start_initialized_)
    return 0;
  int result = InitializeRange(0, 2);
  if (result < 0)
    return result;
  vblank_start_initialized_ = true;
  result = services_.AddIntcHandler(2, callbacks_.vblank_start);
  services_.StoreWord(kHandlerTable, 2);
  services_.StoreWord(kHandlerTable + 4, static_cast<uint32_t>(result));
  services_.EnableIntc(2);
  return result;
}

// Port of func_00134828.
int VsyncModel::RegisterVblankEnd() {
  if (vblank_end_initialized_)
    return 0;
  int result = InitializeRange(2, 4);
  if (result < 0)
    return result;
  vblank_end_initialized_ = true;
  result = services_.AddIntcHandler(3, callbacks_.vblank_end);
  services_.StoreWord(kHandlerTable + 8, 3);
  services_.StoreWord(kHandlerTable + 12, static_cast<uint32_t>(result));
  services_.EnableIntc(3);
  return result;
}

// Port of func_001348A8.
int VsyncModel::Init(int mode) {
  if (mode == 1)
    return RegisterVblankStart();
  if (mode == 2)
    return RegisterVblankEnd();
  const int result = RegisterVblankStart();
  return result < 0 ? result : RegisterVblankEnd();
}

// Port of func_00134928.
void VsyncModel::Shutdown() {
  RemoveHandlers();
  DeleteVsyncSemaphores();
  vblank_start_initialized_ = false;
  vblank_end_initialized_ = false;
}

// Port of func_00134970.
int VsyncModel::WaitPair(int signal_semaphore, int wait_semaphore) {
  if (signal_semaphore < 0 || wait_semaphore < 0) {
    services_.Log("invalid vsync semaphore");
    return 0;
  }
  if (SignalSemaphore(signal_semaphore) != signal_semaphore)
    return 0;
  return WaitSemaphore(wait_semaphore);
}

// Port of func_001349E8.
int VsyncModel::WaitVblankStart() {
  return WaitPair(VsyncSemaphore(0), VsyncSemaphore(1));
}

// Port of func_00134A20.
int VsyncModel::WaitVblankEnd() {
  return WaitPair(VsyncSemaphore(2), VsyncSemaphore(3));
}

// Port of func_00134C60 and func_00134CF0.
void VsyncModel::DumpSemaphore(int id, bool from_interrupt) {
  KernelServices::SemaphoreInfo info{};
  if (services_.ReferSemaphore(id, info, from_interrupt) != id)
    return;
  services_.Log(std::string("semaphore ") + std::to_string(id) + " count " +
                std::to_string(info.current_count));
}

// Port of func_00134CB8 and func_00134D48.
void VsyncModel::DumpAllSemaphores(bool from_interrupt) {
  for (int id = 1; id <= 0x80; ++id)
    DumpSemaphore(id, from_interrupt);
}

// Port of func_00134440 and func_00134480 result checks.
void VsyncModel::ReportSemaphoreResult(std::string_view operation,
                                       int requested, int returned) {
  if (returned == requested)
    return;
  services_.Log(std::string("semaphore ") + std::string(operation) +
                " mismatch " + std::to_string(requested));
}

} // namespace aoe2::kernel
