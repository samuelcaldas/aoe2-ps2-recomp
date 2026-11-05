// Native memory-card boundary: the statically linked Sony libmc (sceMc*).
// The guest library talks to the IOP mcserv over SIF RPC; the emulated IOP has no
// card device (GetInfo returns -11), so the boot stays on the memory card screen.
// These entry points are routed to our fully native host-backed memory card model
// (aoe2::mc::NativeHostMemoryCard), persisting directly into files and folders in mc0/mc1.
// sceMcInit stays on the guest code on purpose: it creates the library semaphore and binds
// the RPC, and every guest sceMc* entry polls that semaphore first (without it they return -200).
#include "game_overrides.h"
#include "ps2_runtime.h"
#include "native_input_module_support.hpp"
#include <aoe2/mc/native_host_mc.hpp>

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace {

class DirectRdramAccess final : public aoe2::runtime::GuestAccess {
public:
    explicit DirectRdramAccess(uint8_t* rdram) : rdram_(rdram) {}

    uint8_t Load8(uint32_t address) override { return rdram_[address & 0x01FFFFFF]; }
    uint16_t Load16(uint32_t address) override {
        uint16_t val;
        std::memcpy(&val, &rdram_[address & 0x01FFFFFF], 2);
        return val;
    }
    uint32_t Load32(uint32_t address) override {
        uint32_t val;
        std::memcpy(&val, &rdram_[address & 0x01FFFFFF], 4);
        return val;
    }
    void Store8(uint32_t address, uint8_t value) override { rdram_[address & 0x01FFFFFF] = value; }
    void Store16(uint32_t address, uint16_t value) override {
        std::memcpy(&rdram_[address & 0x01FFFFFF], &value, 2);
    }
    void Store32(uint32_t address, uint32_t value) override {
        std::memcpy(&rdram_[address & 0x01FFFFFF], &value, 4);
    }
    void ReadBytes(uint32_t address, uint8_t* out, std::size_t count) override {
        std::memcpy(out, &rdram_[address & 0x01FFFFFF], count);
    }
    void WriteBytes(uint32_t address, const uint8_t* in, std::size_t count) override {
        std::memcpy(&rdram_[address & 0x01FFFFFF], in, count);
    }
    void ReportAssertion(std::string_view, int, std::string_view) override {}

private:
    uint8_t* rdram_{nullptr};
};

std::string ReadGuestString(uint8_t* rdram, uint32_t addr, std::size_t max_len = 1024) {
    if (addr == 0 || !rdram) return {};
    std::string str;
    for (std::size_t i = 0; i < max_len; ++i) {
        const char c = static_cast<char>(rdram[(addr + i) & 0x01FFFFFF]);
        if (c == '\0') break;
        str.push_back(c);
    }
    return str;
}

void FinishCall(R5900Context* ctx, int32_t return_val = 0) {
    setReturnS32(ctx, return_val);
    ctx->pc = getRegU32(ctx, 31);
}

void NativeSceMcGetInfo(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    DirectRdramAccess mem(rdram);
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const uint32_t type_ptr = getRegU32(ctx, 6);
    const uint32_t free_ptr = getRegU32(ctx, 7);
    const uint32_t formatted_ptr = getRegU32(ctx, 8); // t0 ($8 in MIPS ABI)
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcGetInfo(port, slot, type_ptr, free_ptr, formatted_ptr, mem);
    FinishCall(ctx, 0);
}

void NativeSceMcSync(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    DirectRdramAccess mem(rdram);
    const int32_t mode = static_cast<int32_t>(getRegU32(ctx, 4));
    const uint32_t cmd_ptr = getRegU32(ctx, 5);
    const uint32_t result_ptr = getRegU32(ctx, 6);
    const int32_t res = aoe2::mc::NativeHostMemoryCard::Instance().SceMcSync(mode, cmd_ptr, result_ptr, mem);
    FinishCall(ctx, res);
}

void NativeSceMcGetDir(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    DirectRdramAccess mem(rdram);
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const std::string path = ReadGuestString(rdram, getRegU32(ctx, 6));
    const uint32_t mode = getRegU32(ctx, 7);
    const int32_t max_entries = static_cast<int32_t>(getRegU32(ctx, 8)); // t0
    const uint32_t table_guest_addr = getRegU32(ctx, 9); // t1
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcGetDir(port, slot, path, mode, max_entries, table_guest_addr, mem);
    FinishCall(ctx, 0);
}

void NativeSceMcOpen(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    DirectRdramAccess mem(rdram);
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const std::string path = ReadGuestString(rdram, getRegU32(ctx, 6));
    const uint32_t flags = getRegU32(ctx, 7);
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcOpen(port, slot, path, flags, mem);
    FinishCall(ctx, 0);
}

void NativeSceMcClose(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcClose(fd);
    FinishCall(ctx, 0);
}

void NativeSceMcSeek(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t offset = static_cast<int32_t>(getRegU32(ctx, 5));
    const int32_t whence = static_cast<int32_t>(getRegU32(ctx, 6));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcSeek(fd, offset, whence);
    FinishCall(ctx, 0);
}

void NativeSceMcRead(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    DirectRdramAccess mem(rdram);
    const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
    const uint32_t buffer_guest_addr = getRegU32(ctx, 5);
    const int32_t size = static_cast<int32_t>(getRegU32(ctx, 6));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcRead(fd, buffer_guest_addr, size, mem);
    FinishCall(ctx, 0);
}

void NativeSceMcWrite(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    DirectRdramAccess mem(rdram);
    const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
    const uint32_t buffer_guest_addr = getRegU32(ctx, 5);
    const int32_t size = static_cast<int32_t>(getRegU32(ctx, 6));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcWrite(fd, buffer_guest_addr, size, mem);
    FinishCall(ctx, 0);
}

void NativeSceMcFlush(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    const int32_t fd = static_cast<int32_t>(getRegU32(ctx, 4));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcFlush(fd);
    FinishCall(ctx, 0);
}

void NativeSceMcMkdir(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const std::string path = ReadGuestString(rdram, getRegU32(ctx, 6));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcMkdir(port, slot, path);
    FinishCall(ctx, 0);
}

void NativeSceMcChdir(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    DirectRdramAccess mem(rdram);
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const std::string path = ReadGuestString(rdram, getRegU32(ctx, 6));
    const uint32_t current_dir_ptr = getRegU32(ctx, 7);
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcChdir(port, slot, path, current_dir_ptr, mem);
    FinishCall(ctx, 0);
}

void NativeSceMcSetFileInfo(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    DirectRdramAccess mem(rdram);
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const std::string path = ReadGuestString(rdram, getRegU32(ctx, 6));
    const uint32_t info_guest_addr = getRegU32(ctx, 7);
    const uint32_t flags = getRegU32(ctx, 8); // t0
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcSetFileInfo(port, slot, path, info_guest_addr, flags, mem);
    FinishCall(ctx, 0);
}

void NativeSceMcDelete(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const std::string path = ReadGuestString(rdram, getRegU32(ctx, 6));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcDelete(port, slot, path);
    FinishCall(ctx, 0);
}

void NativeSceMcFormat(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcFormat(port, slot);
    FinishCall(ctx, 0);
}

void NativeSceMcUnformat(uint8_t*, R5900Context* ctx, PS2Runtime*) {
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcUnformat(port, slot);
    FinishCall(ctx, 0);
}

void NativeSceMcGetEntSpace(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const std::string path = ReadGuestString(rdram, getRegU32(ctx, 6));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcGetEntSpace(port, slot, path);
    FinishCall(ctx, 0);
}

void NativeSceMcRename(uint8_t* rdram, R5900Context* ctx, PS2Runtime*) {
    const int32_t port = static_cast<int32_t>(getRegU32(ctx, 4));
    const int32_t slot = static_cast<int32_t>(getRegU32(ctx, 5));
    const std::string old_path = ReadGuestString(rdram, getRegU32(ctx, 6));
    const std::string new_path = ReadGuestString(rdram, getRegU32(ctx, 7));
    aoe2::mc::NativeHostMemoryCard::Instance().SceMcRename(port, slot, old_path, new_path);
    FinishCall(ctx, 0);
}

struct LibmcEntry {
    uint32_t address;
    PS2Runtime::RecompiledFunction handler;
    const char* name;
};

const std::array<LibmcEntry, 17> kEntries = {{
    {0x00110A78, &NativeSceMcGetInfo, "sceMcGetInfo"},
    {0x00110940, &NativeSceMcSync, "sceMcSync"},
    {0x00110C00, &NativeSceMcGetDir, "sceMcGetDir"},
    {0x001102A0, &NativeSceMcOpen, "sceMcOpen"},
    {0x00110410, &NativeSceMcClose, "sceMcClose"},
    {0x001104C8, &NativeSceMcSeek, "sceMcSeek"},
    {0x00110630, &NativeSceMcRead, "sceMcRead"},
    {0x00110750, &NativeSceMcWrite, "sceMcWrite"},
    {0x00111120, &NativeSceMcFlush, "sceMcFlush"},
    {0x001103D8, &NativeSceMcMkdir, "sceMcMkdir"},
    {0x00110DE0, &NativeSceMcChdir, "sceMcChdir"},
    {0x001111D8, &NativeSceMcSetFileInfo, "sceMcSetFileInfo"},
    {0x00110FF8, &NativeSceMcDelete, "sceMcDelete"},
    {0x00110F30, &NativeSceMcFormat, "sceMcFormat"},
    {0x00111518, &NativeSceMcUnformat, "sceMcUnformat"},
    {0x001115E0, &NativeSceMcGetEntSpace, "sceMcGetEntSpace"},
    {0x001113B0, &NativeSceMcRename, "sceMcRename"},
}};

void InstallNativeMc(PS2Runtime& runtime) {
    const auto switches = aoe2::game_hooks::native_input::ReadSwitches();
    if (!switches.Enabled("memory_card")) {
        std::cout << "[aoe2] native libmc: off\n";
        return;
    }
    std::size_t installed = 0;
    for (const auto& entry : kEntries) {
        if (!runtime.hasFunction(entry.address)) continue;
        if (runtime.replaceFunction(entry.address, entry.handler)) ++installed;
    }
    std::cout << "[aoe2] native libmc: installed " << installed << "/" << kEntries.size() << " functions\n";
}

}  // namespace

PS2_REGISTER_GAME_OVERRIDE("aoe2 native libmc module", "AOK.ELF", 0, 0, InstallNativeMc)
