#pragma once

#include <aoe2/pad/pad_guest_memory.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Sparse little-endian guest memory for the kt_libpad tests; unwritten bytes read as zero.
class FakePadGuestMemory final : public aoe2::pad::PadGuestMemory {
public:
    uint8_t Load8(uint32_t address) override {
        const auto found = bytes.find(address);
        return found == bytes.end() ? 0 : found->second;
    }
    uint16_t Load16(uint32_t address) override { return static_cast<uint16_t>(Load8(address) | (Load8(address + 1) << 8)); }
    uint32_t Load32(uint32_t address) override {
        return Load16(address) | (static_cast<uint32_t>(Load16(address + 2)) << 16);
    }
    void Store8(uint32_t address, uint8_t value) override { bytes[address] = value; }
    void Store16(uint32_t address, uint16_t value) override {
        Store8(address, static_cast<uint8_t>(value));
        Store8(address + 1, static_cast<uint8_t>(value >> 8));
    }
    void Store32(uint32_t address, uint32_t value) override {
        Store16(address, static_cast<uint16_t>(value));
        Store16(address + 2, static_cast<uint16_t>(value >> 16));
    }
    void ReadBytes(uint32_t address, uint8_t* out, std::size_t count) override {
        for (std::size_t i = 0; i < count; ++i) out[i] = Load8(address + static_cast<uint32_t>(i));
    }
    void WriteBytes(uint32_t address, const uint8_t* in, std::size_t count) override {
        for (std::size_t i = 0; i < count; ++i) Store8(address + static_cast<uint32_t>(i), in[i]);
    }
    void ReportAssertion(std::string_view file, int line, std::string_view expression) override {
        assertions.push_back({std::string(file), line, std::string(expression)});
    }

    struct Assertion {
        std::string file;
        int line;
        std::string expression;
    };
    std::map<uint32_t, uint8_t> bytes;
    std::vector<Assertion> assertions;
};
