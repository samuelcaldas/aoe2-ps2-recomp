#include <csetjmp>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <typeinfo>
#include <switch.h>

// Provide the .nx-module-name section explicitly when linking with lld
// (which does not synthesize .nx-module-name via the BFD-only --nx-module-name flag).
namespace {
struct NxModuleName {
    uint32_t zero;
    uint32_t name_len;
    char name[10];
};
__attribute__((section(".nx-module-name"), used))
constexpr NxModuleName kNxModuleName = {0u, 9u, "aoe2_game"};
}  // namespace

extern "C" {
extern void (*__preinit_array_start[])(void) __attribute__((weak));
extern void (*__preinit_array_end[])(void) __attribute__((weak));
extern void (*__init_array_start[])(void) __attribute__((weak));
extern void (*__init_array_end[])(void) __attribute__((weak));
extern void _init(void);

void __libc_init_array(void) {
    svcOutputDebugString("[aoe2-switch] __libc_init_array START\n", 37);

    if (__preinit_array_start && __preinit_array_end) {
        size_t count = __preinit_array_end - __preinit_array_start;
        for (size_t i = 0; i < count; i++) {
            if (__preinit_array_start[i]) {
                __preinit_array_start[i]();
            }
        }
    }

    _init();

    if (__init_array_start && __init_array_end) {
        size_t count = __init_array_end - __init_array_start;
        char msg[128];
        int len = std::snprintf(msg, sizeof(msg), "[aoe2-switch] __init_array count = %zu\n", count);
        svcOutputDebugString(msg, len);

        for (size_t i = 0; i < count; i++) {
            len = std::snprintf(msg, sizeof(msg), "[aoe2-switch] init_array[%zu/%zu] -> %p\n", i, count, (void*)__init_array_start[i]);
            svcOutputDebugString(msg, len);

            if (__init_array_start[i]) {
                __init_array_start[i]();
            }

            len = std::snprintf(msg, sizeof(msg), "[aoe2-switch] init_array[%zu/%zu] done\n", i, count);
            svcOutputDebugString(msg, len);
        }
    }

    svcOutputDebugString("[aoe2-switch] __libc_init_array FINISHED\n", 40);
}

[[noreturn]] void abort(void) noexcept {
    svcOutputDebugString("[aoe2-switch] === abort() CALLED ===\n", 37);
    void* caller = __builtin_return_address(0);
    char buf[128];
    snprintf(buf, sizeof(buf), "[aoe2-switch] abort() caller = %p\n", caller);
    svcOutputDebugString(buf, strlen(buf));
    while (1) {
        svcSleepThread(100000000ULL);
    }
}

void __assert_func(const char *file, int line, const char *func, const char *failedexpr) noexcept {
    char buf[512];
    snprintf(buf, sizeof(buf), "[aoe2-switch] ASSERTION FAILED: %s:%d: %s: %s\n",
             file ? file : "<?>", line, func ? func : "<?>", failedexpr ? failedexpr : "<?>");
    svcOutputDebugString(buf, strlen(buf));
    abort();
}

void __assert(const char *file, int line, const char *failedexpr) noexcept {
    __assert_func(file, line, nullptr, failedexpr);
}
}  // extern "C"

static void switch_signal_handler(int sig) {
    char buf[128];
    snprintf(buf, sizeof(buf), "[aoe2-switch] SIGNAL CAUGHT: %d caller=%p\n", sig, __builtin_return_address(0));
    svcOutputDebugString(buf, strlen(buf));
    while (1) {
        svcSleepThread(100000000ULL);
    }
}

extern "C" void switch_early_terminate() noexcept {
    svcOutputDebugString("[FATAL-TERMINATE] std::terminate() CALLED!\n", 43);
    void* caller = __builtin_return_address(0);
    char buf[128];
    snprintf(buf, sizeof(buf), "[FATAL-TERMINATE] caller = %p\n", caller);
    svcOutputDebugString(buf, strlen(buf));
    while (1) {
        svcSleepThread(100000000ULL);
    }
}

extern "C" void (*switch_get_early_terminate_handler())(void) {
    return switch_early_terminate;
}

extern "C" void userAppInit(void) {
    std::set_terminate(switch_early_terminate);
    signal(SIGABRT, switch_signal_handler);
    signal(SIGSEGV, switch_signal_handler);
    signal(SIGILL, switch_signal_handler);
    signal(SIGFPE, switch_signal_handler);
    signal(SIGBUS, switch_signal_handler);
    svcOutputDebugString("[aoe2-switch] userAppInit CALLED with early terminate & signal handlers\n", 73);
}
