// Window-message dispatch: the game's DispatchMessage (generated unit, linked, not copied) loads
// message, wParam and lParam from the MSG and calls the registered window procedure with hwnd 1.
// The overlay/boot registration must keep that contract; a replacement that skips the MSG loads
// delivers stale registers, and screens waiting for a posted message never start.
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

extern void FUN_00226630_0x226630(uint8_t*, R5900Context*, PS2Runtime*);
int RegisterGeneratedOverlays(PS2Runtime& runtime);

namespace {
constexpr uint32_t DISPATCH = 0x226630, WNDPROC = 0x1842F0;
constexpr uint32_t GP = 0x00500000, WNDPROC_SLOT = GP - 0x746C;
constexpr uint32_t MSG = 0x2000, STACK = 0x100000, RETURN = 0x8000;
constexpr uint32_t MESSAGE = 0x520, WPARAM = 0xB, LPARAM = 0x77, STALE = 0xC6F1FC;

struct Delivered {
    uint32_t hwnd, message, wparam, lparam;
    unsigned calls;
} delivered;

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void window_procedure(uint8_t*, R5900Context* context, PS2Runtime*) {
    delivered = {getRegU32(context, 4), getRegU32(context, 5), getRegU32(context, 6), getRegU32(context, 7),
                 delivered.calls + 1};
    SET_GPR_U32(context, 2, 0x5A);
    context->pc = getRegU32(context, 31);
}

void write_word(uint8_t* rdram, uint32_t address, uint32_t value) {
    std::memcpy(rdram + address, &value, sizeof(value));
}

void dispatch_posted_message(PS2Runtime& runtime) {
    auto* rdram = runtime.memory().getRDRAM();
    write_word(rdram, WNDPROC_SLOT, WNDPROC);
    const uint32_t message[] = {1, MESSAGE, WPARAM, LPARAM};
    std::memcpy(rdram + MSG, message, sizeof(message));
    R5900Context context{};
    SET_GPR_U32(&context, 28, GP);
    SET_GPR_U32(&context, 29, STACK);
    SET_GPR_U32(&context, 31, RETURN);
    SET_GPR_U32(&context, 4, MSG);
    SET_GPR_U32(&context, 5, STALE);
    context.pc = DISPATCH;
    delivered = {};
    runtime.lookupFunction(DISPATCH)(rdram, &context, &runtime);
    check(delivered.calls == 1, "window procedure was not called exactly once");
    check(delivered.hwnd == 1, "window procedure did not receive hwnd 1");
    check(delivered.message == MESSAGE, "window procedure received a stale message number");
    check(delivered.wparam == WPARAM && delivered.lparam == LPARAM, "window procedure lost wParam/lParam");
    check(getRegU32(&context, 2) == 0x5A, "DispatchMessage did not return the procedure's result");
    check(context.pc == RETURN, "DispatchMessage did not return to its caller");
}

PS2Runtime& fresh_runtime(PS2Runtime& runtime) {
    check(runtime.memory().initialize(), "memory initialization failed");
    runtime.registerFunction(DISPATCH, FUN_00226630_0x226630);
    runtime.registerFunction(WNDPROC, window_procedure);
    return runtime;
}

void generated_dispatch_delivers_the_message_fields() {
    PS2Runtime runtime;
    dispatch_posted_message(fresh_runtime(runtime));
}

void overlay_registration_keeps_the_dispatch_contract() {
    PS2Runtime runtime;
    fresh_runtime(runtime);
    RegisterGeneratedOverlays(runtime);
    dispatch_posted_message(runtime);
}
}

int main() {
    try {
        generated_dispatch_delivers_the_message_fields();
        overlay_registration_keeps_the_dispatch_contract();
    } catch (const std::exception& error) {
        std::cerr << "message_dispatch: " << error.what() << '\n';
        return 1;
    }
    std::cout << "message_dispatch: ok\n";
    return 0;
}
