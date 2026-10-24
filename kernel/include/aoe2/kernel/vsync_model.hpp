// Origin: samuelcaldas/age-of-empires-2-decomp, commit
// af30b177615b34ba35f5c542dc8edfa302da818d (working tree),
// src/kcet/kt_kernel.c.
#pragma once

#include <aoe2/kernel/kernel_services.hpp>

#include <cstdint>

namespace aoe2::kernel {

struct VsyncCallbacks {
  uint32_t vblank_start;
  uint32_t vblank_end;
};

class VsyncModel {
public:
  VsyncModel(KernelServices &services, VsyncCallbacks callbacks);
  int Init(int mode);
  void Shutdown();
  int WaitVblankStart();
  int WaitVblankEnd();
  int OnVblankStart();
  int OnVblankEnd();
  int VsyncSemaphore(int index);
  int CreateSemaphore(int init, int max, int option);
  int SignalSemaphore(int id);
  int SignalSemaphoreFromInterrupt(int id);
  int DeleteSemaphore(int id);
  int WaitSemaphore(int id);
  int DrainSemaphore(int id);
  int DrainSemaphoreReporting(int id);
  void DumpSemaphore(int id, bool from_interrupt);
  void DumpAllSemaphores(bool from_interrupt);

private:
  int InitializeRange(int start, int end);
  int RegisterVblankStart();
  int RegisterVblankEnd();
  void RemoveHandlers();
  void DeleteVsyncSemaphores();
  int WaitPair(int signal_semaphore, int wait_semaphore);
  void ReportSemaphoreResult(std::string_view operation, int requested,
                             int returned);

  KernelServices &services_;
  VsyncCallbacks callbacks_;
  bool vblank_start_initialized_ = false;
  bool vblank_end_initialized_ = false;
};

} // namespace aoe2::kernel
