#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace aoe2::runtime {

// Standard PS2 EE Main RAM is 32 MB
constexpr size_t kEeRamSize = 32 * 1024 * 1024;

class GuestMemory {
public:
    GuestMemory();
    ~GuestMemory() = default;

    bool Initialize(size_t size = kEeRamSize);
    void Reset();

    uint8_t Read8(uint32_t addr) const;
    uint16_t Read16(uint32_t addr) const;
    uint32_t Read32(uint32_t addr) const;

    void Write8(uint32_t addr, uint8_t val);
    void Write16(uint32_t addr, uint16_t val);
    void Write32(uint32_t addr, uint32_t val);

    uint8_t* GetPointer(uint32_t addr);
    const uint8_t* GetPointer(uint32_t addr) const;

    size_t GetSize() const { return memory_.size(); }

private:
    std::vector<uint8_t> memory_;
};

class PlatformServices {
public:
    virtual ~PlatformServices() = default;
    virtual void LogInfo(const char* message) = 0;
    virtual void LogError(const char* message) = 0;
    virtual uint64_t GetMicroseconds() = 0;
};

} // namespace aoe2::runtime
