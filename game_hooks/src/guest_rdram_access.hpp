#pragma once

#include <aoe2/runtime/guest_access.hpp>

#include "runtime/ps2_memory.h"
// Upstream Common.h has no include guard; snapshot diagnostics include it through this header too.
#include "../../third_party/PS2Recomp/ps2xRuntime/src/lib/Kernel/Syscalls/Common.h"

#include <cstring>
#include <iostream>

namespace aoe2::game_hooks {

// Guest RAM seen through the runtime's address translation; a bad address is reported once and reads as zero.
class RdramGuestAccess final : public aoe2::runtime::GuestAccess {
public:
    void Bind(uint8_t* rdram) { rdram_ = rdram; }
    uint8_t Load8(uint32_t address) override { return Load<uint8_t>(address); }
    uint16_t Load16(uint32_t address) override { return Load<uint16_t>(address); }
    uint32_t Load32(uint32_t address) override { return Load<uint32_t>(address); }
    void Store8(uint32_t address, uint8_t value) override { Store(address, value); }
    void Store16(uint32_t address, uint16_t value) override { Store(address, value); }
    void Store32(uint32_t address, uint32_t value) override { Store(address, value); }
    void ReadBytes(uint32_t address, uint8_t* out, std::size_t count) override {
        const uint8_t* source = Resolve(address, count);
        if (!source) {
            Bad(address);
            if (count) std::memset(out, 0, count);
            return;
        }
        if (count) std::memcpy(out, source, count);
    }
    void WriteBytes(uint32_t address, const uint8_t* in, std::size_t count) override {
        uint8_t* destination = Resolve(address, count);
        if (!destination) return Bad(address);
        if (count) std::memcpy(destination, in, count);
    }

    void ReportAssertion(std::string_view file, int line, std::string_view expression) override {
        std::cerr << "[native-libpad] ASSERT " << file << ':' << line << " (0x" << std::hex << line << std::dec << ") "
                  << expression << std::endl;
    }

private:
    uint8_t* Resolve(uint32_t address, std::size_t count) {
        uint32_t offset = 0;
        bool scratch = false;
        if (!rdram_ || !resolveEeGuestRange(address, count, offset, scratch)) return nullptr;
        uint8_t* base = scratch ? ps2GetScratchpadHostPtr() : rdram_;
        return base ? base + offset : nullptr;
    }
    template <typename T>
    T Load(uint32_t address) {
        T value{};
        const uint8_t* source = Resolve(address, sizeof(T));
        if (source == nullptr) return Bad(address), value;
        std::memcpy(&value, source, sizeof(T));
        return value;
    }
    template <typename T>
    void Store(uint32_t address, T value) {
        uint8_t* destination = Resolve(address, sizeof(T));
        if (destination == nullptr) return Bad(address);
        std::memcpy(destination, &value, sizeof(T));
    }
    void Bad(uint32_t address) {
        if (reported_++ < 8) std::cerr << "[native-libpad] bad guest address 0x" << std::hex << address << std::dec << std::endl;
    }
    uint8_t* rdram_ = nullptr;
    int reported_ = 0;
};

}  // namespace aoe2::game_hooks
