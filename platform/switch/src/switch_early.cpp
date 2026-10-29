#include <exception>
#include <string>
#include <switch.h>

struct EeDispatcherTransfer {};

void switch_early_terminate() {
    svcOutputDebugString("[FATAL-TERMINATE] Early std::terminate called!\n", 49);
    std::exception_ptr p = std::current_exception();
    if (p) {
        try {
            std::rethrow_exception(p);
        } catch (const EeDispatcherTransfer&) {
            svcOutputDebugString("[FATAL-TERMINATE] EeDispatcherTransfer ESCAPED to std::terminate!\n", 67);
        } catch (const std::exception& e) {
            std::string msg = std::string("[FATAL-TERMINATE] Exception: ") + e.what() + "\n";
            svcOutputDebugString(msg.c_str(), msg.length());
        } catch (...) {
            svcOutputDebugString("[FATAL-TERMINATE] Completely Unknown exception\n", 48);
        }
    } else {
        svcOutputDebugString("[FATAL-TERMINATE] No active exception\n", 39);
    }
}

extern "C" void (*switch_get_early_terminate_handler())(void) {
    return switch_early_terminate;
}

extern "C" void userAppInit(void) {
    std::set_terminate(switch_early_terminate);
    svcOutputDebugString("[aoe2-switch] userAppInit CALLED with early terminate handler\n", 63);
}
