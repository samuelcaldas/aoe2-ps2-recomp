#include <aoe2/runtime/deci2_runtime_session.hpp>
#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"
#include "ps2_syscalls.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
class FailedStderr {
public:
    FailedStderr() : state_(std::cerr.rdstate()), mask_(std::cerr.exceptions()) {
        std::cerr.exceptions(std::ios::goodbit);
        std::cerr.setstate(std::ios::badbit);
    }
    ~FailedStderr() {
        std::cerr.clear(state_);
        std::cerr.exceptions(mask_);
    }
private:
    std::ios::iostate state_;
    std::ios::iostate mask_;
};
}

void verify_deci2_kputs_failure() {
    PS2Runtime runtime;
    if (!runtime.memory().initialize()) throw std::runtime_error("memory initialization failed");
    aoe2::runtime::Deci2RuntimeSession session(runtime);
    auto* memory = runtime.memory().getRDRAM();
    const std::uint32_t argument = 1024;
    std::memcpy(memory + 512, &argument, sizeof(argument));
    std::memcpy(memory + argument, "test", 5);
    R5900Context context{};
    SET_GPR_U32(&context, 4, 16);
    SET_GPR_U32(&context, 5, 512);
    bool escaped = false;
    {
        FailedStderr failure;
        try { ps2_syscalls::Deci2Call(memory, &context, &runtime); }
        catch (const std::ios_base::failure&) { escaped = true; }
    }
    if (escaped) throw std::runtime_error("kputs sink failure escaped syscall boundary");
    if (static_cast<int>(getRegU32(&context, 2)) != -1)
        throw std::runtime_error("kputs sink failure reported success");
}
