#include "game_overrides.h"
#include "guest_rdram_access.hpp"
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "runtime/ee_scheduler.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

extern void FUN_00129b10_0x129b10(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_0012a650_0x12a650(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_001295d0_0x1295d0(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_00129588_0x129588(uint8_t*, R5900Context*, PS2Runtime*);
extern void FUN_0012bc20_0x12bc20(uint8_t*, R5900Context*, PS2Runtime*);
void run_scmd_completion_tests(PS2Runtime& runtime);
namespace {
// Contract reference: samuelcaldas/age-of-empires-2-decomp, kt_libcdvd_server.c,
// eb2b12731694310da08d9b6501943169f99f4653. Generated code is linked, not copied.
constexpr uint32_t MEDIA = 0x129b10, CALLER = 0x12a650, OUTPUT = 0x4ed300;
constexpr uint32_t STACK = 0x100000, RETURN = 0x8000;
using aoe2::game_hooks::RdramGuestAccess;
enum class Scenario { Accept, Reject, Retry };
Scenario scenario;
unsigned checks, retries, messages;
bool finished, suspend_dependency;
R5900Context result;
int32_t dependency_value;
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void reply(R5900Context* context, int32_t value) {
    SET_GPR_S32(context, 2, value);
    context->pc = getRegU32(context, 31);
}
void media_dependency(uint8_t*, R5900Context* context, PS2Runtime* runtime) {
    ++checks;
    check(checks <= 2, "unbounded media retry");
    dependency_value = scenario == Scenario::Accept || checks == 2 ? 1 : 0;
    reply(context, dependency_value);
    if (!suspend_dependency) return;
    context->pc = 0x9000;
    auto& scheduler = runtime->eeScheduler();
    const int id = scheduler.createSemaphore(0, 1, 0, 0);
    check(id > 0, "create dependency semaphore");
    scheduler.scheduleHostCallback(100, [runtime, id] {
        check(runtime->eeScheduler().semaphore(id)->waiters.size() == 1, "dependency did not suspend");
        runtime->eeScheduler().signalSemaphore(id, false);
    });
    scheduler.waitSemaphore(id);
}
void resume_dependency(uint8_t*, R5900Context* context, PS2Runtime*) {
    reply(context, dependency_value);
}
void disc_type(uint8_t*, R5900Context* context, PS2Runtime*) {
    reply(context, scenario == Scenario::Retry ? 1 : 5);
}
void classify(uint8_t*, R5900Context* context, PS2Runtime*) {
    check(getRegU32(context, 5) == 7 && getRegU32(context, 7) == 0xffffffff, "classification arguments");
    reply(context, -1);
}
void tray(uint8_t*, R5900Context* context, PS2Runtime*) { reply(context, 0); }
void ready(uint8_t*, R5900Context* context, PS2Runtime*) {
    check(getRegU32(context, 4) == 1, "DiskReady mode");
    reply(context, 2);
}
void retry(uint8_t*, R5900Context* context, PS2Runtime*) {
    check(++retries <= 1, "caller exceeded bounded retry");
    context->pc = getRegU32(context, 31);
}
void message(uint8_t*, R5900Context* context, PS2Runtime*) {
    ++messages;
    context->pc = getRegU32(context, 31);
}
void finish(uint8_t*, R5900Context* context, PS2Runtime* runtime) {
    finished = true;
    result = *context;
    runtime->eeScheduler().requestStop();
}
void install(PS2Runtime& runtime, const char* switches) {
    const auto saved = PS2Runtime::getIoPaths();
    auto paths = saved;
    paths.elfDirectory = std::filesystem::temp_directory_path() /
        ("aoe2-media-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check(std::filesystem::create_directory(paths.elfDirectory), "create media fixture config");
    std::ofstream(paths.elfDirectory / "native_modules.ini") << switches;
    PS2Runtime::setIoPaths(paths);
    ps2_game_overrides::applyMatching(runtime, "AOK.ELF", 0, 0, false);
    PS2Runtime::setIoPaths(saved);
    std::filesystem::remove_all(paths.elfDirectory);
}
void register_functions(PS2Runtime& runtime) {
    runtime.registerFunction(0x12bc20, FUN_0012bc20_0x12bc20);
    runtime.registerFunction(0x1295d0, FUN_001295d0_0x1295d0);
    runtime.registerFunction(0x129614, FUN_001295d0_0x1295d0);
    runtime.registerFunction(MEDIA, FUN_00129b10_0x129b10);
    for (auto address : {0x129b40u, 0x129b50u, 0x129b6cu})
        runtime.registerFunction(address, FUN_00129b10_0x129b10);
    runtime.registerFunction(CALLER, FUN_0012a650_0x12a650);
    for (auto address : {0x12a678u, 0x12a680u, 0x12a688u, 0x12a698u, 0x12a6b0u, 0x12a710u})
        runtime.registerFunction(address, FUN_0012a650_0x12a650);
    runtime.registerFunction(0x129fd0, media_dependency);
    runtime.registerFunction(0x12dbc0, disc_type);
    runtime.registerFunction(0x129588, classify);
    runtime.registerFunction(0x12da38, tray);
    runtime.registerFunction(0x12db80, ready);
    runtime.registerFunction(0x12aaa0, retry);
    runtime.registerFunction(0x118de0, message);
    runtime.registerFunction(RETURN, finish);
    runtime.registerFunction(0x9000, resume_dependency);
}
void run(PS2Runtime& runtime, uint32_t entry, uint32_t initial_return, uint32_t argument = OUTPUT) {
    auto* ram = runtime.memory().getRDRAM();
    RdramGuestAccess memory; memory.Bind(ram);
    memory.Store32(OUTPUT, 0x40);
    memory.Store32(OUTPUT + 4, 0xaaaaaaaa);
    memory.Store32(OUTPUT + 8, 0xbbbbbbbb);
    memory.Store32(OUTPUT + 12, 0xcccccccc);
    R5900Context context{};
    for (unsigned index = 16; index <= 23; ++index)
        SET_GPR_U64(&context, index, 0x1234567800000000ull + index);
    SET_GPR_U32(&context, 29, STACK); SET_GPR_U32(&context, 31, RETURN);
    SET_GPR_U32(&context, 4, argument); SET_GPR_U32(&context, 2, initial_return);
    checks = retries = messages = 0; finished = false; context.pc = entry;
    runtime.eeScheduler().reset(ram, context);
    runtime.eeScheduler().run();
    check(finished, "guest did not return");
    check(getRegU32(&result, 29) == STACK, "guest stack corrupted");
    for (unsigned index = 16; index <= 23; ++index)
        check(GPR_U64((&result), index) == 0x1234567800000000ull + index, "callee-saved register corrupted");
    check(memory.Load32(OUTPUT) == 0x40 && memory.Load32(OUTPUT + 12) == 0xcccccccc, "output guards corrupted");
}
void media_check_preserves_original_return_and_output(PS2Runtime& runtime) {
    scenario = Scenario::Accept;
    for (auto initial : {0u, 1u, 2u}) {
        run(runtime, MEDIA, initial);
        RdramGuestAccess memory; memory.Bind(runtime.memory().getRDRAM());
        check(getRegU32(&result, 2) == 0, "media classification retained stale v0");
        check(memory.Load32(OUTPUT + 4) == 0 && memory.Load32(OUTPUT + 8) == 0, "media output not calculated");
        check(checks == 1, "original media dependency not called");
    }
}
void ready_disc_is_not_rejected_by_stale_v0(PS2Runtime& runtime) {
    scenario = Scenario::Accept;
    run(runtime, CALLER, 2);
    check(getRegU32(&result, 2) == 1 && messages == 0, "ready disc rejected by stale v0");
}
void initialization_preserves_media_mask(PS2Runtime& runtime) {
    runtime.registerFunction(0x129588, FUN_00129588_0x129588);
    RdramGuestAccess memory; memory.Bind(runtime.memory().getRDRAM());
    memory.Store32(0x48f4d0, 7); memory.Store32(0x48f4d4, 0x40);
    memory.Store32(0x48f4d8, 9); memory.Store32(0x48f4dc, 0x80);
    for (auto mode : {7u, 9u, 99u}) {
        run(runtime, 0x1295d0, 0, mode);
        const auto expected = mode == 7 ? 0x40u : mode == 9 ? 0x80u : 7u;
        check(getRegU32(&result, 2) == expected, "Init discarded original accepted-media mask");
    }
}
void rejection_and_retry(PS2Runtime& runtime) {
    scenario = Scenario::Reject; run(runtime, CALLER, 2);
    RdramGuestAccess memory; memory.Bind(runtime.memory().getRDRAM());
    std::cout << "rejection result=" << getRegU32(&result, 2) << " messages=" << messages << " checks=" << checks << " type=" << memory.Load32(OUTPUT + 4) << " class=" << memory.Load32(OUTPUT + 8) << '\n';
    check(getRegU32(&result, 2) == 0 && messages == 1, "rejected disc accepted");
    check(memory.Load32(OUTPUT + 4) == 5 && memory.Load32(OUTPUT + 8) == 3, "rejection classification");
    scenario = Scenario::Retry; run(runtime, CALLER, 2);
    check(getRegU32(&result, 2) == 1 && messages == 0 && retries == 1 && checks == 2, "retry did not converge");
}
}
int main() {
    try {
        PS2Runtime runtime;
        check(runtime.memory().initialize() && runtime.syncCoreSubsystems(), "runtime initialization");
        for (const auto* switches : {"cdvd=off\nfile_cd=off\n", "cdvd=on\nfile_cd=off\n", "cdvd=off\nfile_cd=on\n"}) {
            register_functions(runtime); install(runtime, switches);
            for (bool suspend : {false, true}) {
                suspend_dependency = suspend;
                ready_disc_is_not_rejected_by_stale_v0(runtime);
                media_check_preserves_original_return_and_output(runtime);
                rejection_and_retry(runtime);
            }
            initialization_preserves_media_mask(runtime);
            run_scmd_completion_tests(runtime);
            std::cout << "PASS media mask/return/output, caller, reject/retry, suspend/resume: " << switches;
        }
        return 0;
    } catch (const std::runtime_error& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
