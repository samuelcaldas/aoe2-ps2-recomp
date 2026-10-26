#pragma once

#include <aoe2/runtime/guest_access.hpp>
#include <cstring>
#include <stdexcept>
#include <vector>

class CdvdTestMemory final : public aoe2::runtime::GuestAccess {
public:
    std::vector<uint8_t> ram = std::vector<uint8_t>(32 * 1024 * 1024, 0xa5);
    uint8_t Load8(uint32_t address) override { return ram.at(address); }
    uint16_t Load16(uint32_t address) override { return load<uint16_t>(address); }
    uint32_t Load32(uint32_t address) override { return load<uint32_t>(address); }
    void Store8(uint32_t address, uint8_t value) override { ram.at(address) = value; }
    void Store16(uint32_t address, uint16_t value) override { store(address, value); }
    void Store32(uint32_t address, uint32_t value) override { store(address, value); }
    void ReadBytes(uint32_t address, uint8_t* out, std::size_t count) override {
        validate(address, count);
        std::memcpy(out, ram.data() + address, count);
    }
    void WriteBytes(uint32_t address, const uint8_t* in, std::size_t count) override {
        validate(address, count);
        std::memcpy(ram.data() + address, in, count);
    }
    void ReportAssertion(std::string_view, int, std::string_view) override {}
private:
    void validate(uint32_t address, std::size_t count) {
        if (uint64_t(address) + count > ram.size()) throw std::out_of_range("test guest range");
    }
    template<class Value> Value load(uint32_t address) {
        Value value;
        ReadBytes(address, reinterpret_cast<uint8_t*>(&value), sizeof(value));
        return value;
    }
    template<class Value> void store(uint32_t address, Value value) {
        WriteBytes(address, reinterpret_cast<const uint8_t*>(&value), sizeof(value));
    }
};
