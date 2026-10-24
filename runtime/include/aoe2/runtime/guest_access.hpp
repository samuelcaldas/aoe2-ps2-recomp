#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace aoe2::runtime {

class GuestAccess {
public:
    virtual ~GuestAccess() = default;
    virtual uint8_t Load8(uint32_t address) = 0;
    virtual uint16_t Load16(uint32_t address) = 0;
    virtual uint32_t Load32(uint32_t address) = 0;
    virtual void Store8(uint32_t address, uint8_t value) = 0;
    virtual void Store16(uint32_t address, uint16_t value) = 0;
    virtual void Store32(uint32_t address, uint32_t value) = 0;
    virtual void ReadBytes(uint32_t address, uint8_t* out, std::size_t count) = 0;
    virtual void WriteBytes(uint32_t address, const uint8_t* in, std::size_t count) = 0;
    virtual void ReportAssertion(std::string_view file, int line, std::string_view expression) = 0;
};

}
