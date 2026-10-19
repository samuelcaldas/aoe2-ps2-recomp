#include <aoe2/runtime/guest_memory.hpp>
#include <cstring>
#include <stdexcept>

namespace aoe2::runtime {

GuestMemory::GuestMemory() = default;

bool GuestMemory::Initialize(size_t size) {
    memory_.resize(size, 0);
    return true;
}

void GuestMemory::Reset() {
    std::fill(memory_.begin(), memory_.end(), 0);
}

uint8_t GuestMemory::Read8(uint32_t addr) const {
    if (addr >= memory_.size()) return 0;
    return memory_[addr];
}

uint16_t GuestMemory::Read16(uint32_t addr) const {
    if (addr + 1 >= memory_.size()) return 0;
    uint16_t val;
    std::memcpy(&val, &memory_[addr], sizeof(val));
    return val;
}

uint32_t GuestMemory::Read32(uint32_t addr) const {
    if (addr + 3 >= memory_.size()) return 0;
    uint32_t val;
    std::memcpy(&val, &memory_[addr], sizeof(val));
    return val;
}

void GuestMemory::Write8(uint32_t addr, uint8_t val) {
    if (addr < memory_.size()) {
        memory_[addr] = val;
    }
}

void GuestMemory::Write16(uint32_t addr, uint16_t val) {
    if (addr + 1 < memory_.size()) {
        std::memcpy(&memory_[addr], &val, sizeof(val));
    }
}

void GuestMemory::Write32(uint32_t addr, uint32_t val) {
    if (addr + 3 < memory_.size()) {
        std::memcpy(&memory_[addr], &val, sizeof(val));
    }
}

uint8_t* GuestMemory::GetPointer(uint32_t addr) {
    if (addr >= memory_.size()) return nullptr;
    return &memory_[addr];
}

const uint8_t* GuestMemory::GetPointer(uint32_t addr) const {
    if (addr >= memory_.size()) return nullptr;
    return &memory_[addr];
}

} // namespace aoe2::runtime
