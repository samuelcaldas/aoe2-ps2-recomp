#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <cstring>
#include <stdexcept>

namespace {
// SCmd getter contract: samuelcaldas/age-of-empires-2-decomp, kt_libcdvd_server.c,
// eb2b12731694310da08d9b6501943169f99f4653; the local generated original is linked.
constexpr uint32_t ENTRY = 0xb0000, RESUME = 0xb0010, GETTER = 0x12bc20;
bool completed;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void complete_later(PS2Runtime& runtime, int id) {
    runtime.eeScheduler().scheduleHostCallback(10000, [&runtime, id] {
        check(runtime.eeScheduler().semaphore(id)->waiters.size() == 1, "SCmd waiter did not block on pending work");
        completed = true;
        runtime.eeScheduler().signalSemaphore(id, false);
    });
}
void begin_wait(uint8_t* ram, R5900Context* context, PS2Runtime* runtime) {
    const int id = runtime->eeScheduler().createSemaphore(0, 1, 0, 0);
    check(id > 0, "SCmd completion semaphore creation");
    std::memcpy(ram + 0x4ed34c, &id, sizeof(id));
    SET_GPR_U32(context, 31, RESUME);
    runtime->lookupFunction(GETTER)(ram, context, runtime);
    check(getRegU32(context, 2) == uint32_t(id), "SCmd getter changed ID");
    check(runtime->eeScheduler().semaphore(id)->count == 0, "SCmd getter published completion before work finished");
    complete_later(*runtime, id);
    context->pc = RESUME;
    runtime->eeScheduler().waitSemaphore(id);
}
void resumed(uint8_t*, R5900Context*, PS2Runtime* runtime) {
    check(completed, "SCmd wait returned before actual completion");
    runtime->eeScheduler().requestStop();
}
}
void run_scmd_completion_tests(PS2Runtime& runtime) {
    runtime.registerFunction(ENTRY, begin_wait);
    runtime.registerFunction(RESUME, resumed);
    for (int cycle = 0; cycle < 2; ++cycle) {
        completed = false;
        R5900Context context{}; context.pc = ENTRY;
        runtime.eeScheduler().reset(runtime.memory().getRDRAM(), context);
        runtime.eeScheduler().run();
        check(completed, "actual SCmd completion was not observed");
    }
}
